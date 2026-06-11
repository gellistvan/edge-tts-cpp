#include "communication/EdgeProtocol.hpp"
#include "common/Error.hpp"
#include "core/Chunk.hpp"
#include "serialization/MetadataJsonParser.hpp"
#include "serialization/ProtocolParser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace edge_tts::communication {

namespace {

// -------------------------------------------------------------------------
// Text frame parser
// -------------------------------------------------------------------------

common::Result<std::vector<IncomingMessage>>
parse_text_frame(const std::string& text)
{
    serialization::ProtocolParser parser;
    auto msg_result = parser.parse(text);
    if (!msg_result)
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "text frame parse error",
                          msg_result.error().what()});

    const auto& msg = *msg_result;
    const auto  path = msg.header("Path");

    if (!path)
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "text frame missing Path header"});

    if (*path == "audio.metadata") {
        serialization::MetadataJsonParser meta_parser;
        auto chunks = meta_parser.parse(msg.body);
        if (!chunks)
            return common::Result<std::vector<IncomingMessage>>::fail(
                common::Error{common::ErrorCode::protocol_error,
                              "audio.metadata parse error",
                              chunks.error().what()});

        if (chunks->empty())
            return common::Result<std::vector<IncomingMessage>>::fail(
                common::Error{common::ErrorCode::protocol_error,
                              "No WordBoundary metadata found"});

        std::vector<IncomingMessage> result;
        result.reserve(chunks->size());
        for (auto& bc : *chunks)
            result.push_back(IncomingMessage{
                IncomingMessageKind::boundary,
                core::TtsChunk{std::move(bc)}
            });
        return common::Result<std::vector<IncomingMessage>>::ok(std::move(result));
    }

    if (*path == "turn.end")
        return common::Result<std::vector<IncomingMessage>>::ok(
            {{IncomingMessage{IncomingMessageKind::turn_end, std::nullopt}}});

    if (*path == "response" || *path == "turn.start")
        return common::Result<std::vector<IncomingMessage>>::ok(
            {{IncomingMessage{IncomingMessageKind::ignored, std::nullopt}}});

    return common::Result<std::vector<IncomingMessage>>::fail(
        common::Error{common::ErrorCode::protocol_error,
                      "unknown text frame path", *path});
}

// -------------------------------------------------------------------------
// Binary frame parser
// -------------------------------------------------------------------------

common::Result<std::vector<IncomingMessage>>
parse_binary_frame(const std::vector<std::byte>& data)
{
    // Binary frame format:
    //   bytes 0-1          : big-endian uint16 header_length
    //                        (includes these 2 bytes, excludes \r\n separator)
    //   bytes 2..HL-1      : header content (HL-2 bytes)
    //   bytes HL..HL+1     : \r\n separator
    //   bytes HL+2..end    : body (audio data)

    if (data.size() < 2)
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary message too short for header length"});

    const auto header_length = static_cast<std::size_t>(
        (static_cast<uint8_t>(data[0]) << 8) | static_cast<uint8_t>(data[1]));

    // header_length encodes the 2-byte prefix plus header content, minimum meaningful
    // value is 2; smaller values indicate a malformed frame.
    if (header_length < 2)
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary frame header_length is too small (minimum 2)"});

    if (header_length > data.size())
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary message header_length exceeds message size"});

    // Verify the \r\n separator bytes are present; a truncated frame would otherwise
    // silently produce an empty audio body.
    if (header_length + 2 > data.size())
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary frame missing \\r\\n separator after header"});

    if (data[header_length]     != static_cast<std::byte>('\r') ||
        data[header_length + 1] != static_cast<std::byte>('\n'))
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary frame separator is not \\r\\n"});

    // Parse header content (bytes 2..header_length-1) using ProtocolParser.
    // Append \r\n\r\n so ProtocolParser finds the required separator.
    // header_length >= 2 guaranteed by checks above.
    const std::size_t header_content_len = header_length - 2;
    std::string header_str(
        reinterpret_cast<const char*>(data.data() + 2),
        header_content_len);
    header_str += "\r\n\r\n";

    serialization::ProtocolParser parser;
    auto hdr_result = parser.parse(header_str);
    if (!hdr_result)
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary frame header parse error",
                          hdr_result.error().what()});

    const auto& headers = *hdr_result;

    const auto path = headers.header("Path");
    if (!path || *path != "audio")
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary frame path is not audio",
                          path.value_or("(absent)")});

    const auto content_type = headers.header("Content-Type");
    if (content_type && *content_type != "audio/mpeg")
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "unexpected binary frame Content-Type", *content_type});

    // Body starts immediately after the separator.  Both the existence and
    // correctness of the separator have been verified above.
    const std::size_t body_offset = header_length + 2;
    const std::size_t body_len    = data.size() - body_offset;

    if (!content_type) {
        // No Content-Type: ignore if the body is empty, reject if it has data.
        if (body_len == 0)
            return common::Result<std::vector<IncomingMessage>>::ok(
                {{IncomingMessage{IncomingMessageKind::ignored, std::nullopt}}});

        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary message has no Content-Type but non-empty body"});
    }

    // audio/mpeg frame must have a non-empty body
    if (body_len == 0)
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary audio frame is missing audio data"});

    core::AudioChunk chunk;
    chunk.data.assign(data.begin() + static_cast<std::ptrdiff_t>(body_offset),
                      data.end());

    return common::Result<std::vector<IncomingMessage>>::ok(
        {{IncomingMessage{IncomingMessageKind::audio, core::TtsChunk{std::move(chunk)}}}});
}

} // anonymous namespace

// -------------------------------------------------------------------------
// EdgeProtocol::parse_incoming
// -------------------------------------------------------------------------

common::Result<std::vector<IncomingMessage>>
EdgeProtocol::parse_incoming(const WebSocketMessage& message) const
{
    if (message.type == WebSocketMessage::Type::text)
        return parse_text_frame(message.text);
    return parse_binary_frame(message.binary);
}

} // namespace edge_tts::communication
