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
    // Reference: communicate.py __stream() TEXT branch
    //   encoded_data = received.data.encode("utf-8")
    //   parameters, data = get_headers_and_data(encoded_data, encoded_data.find(b"\r\n\r\n"))
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

    // Reference: path == b"audio.metadata"
    if (*path == "audio.metadata") {
        serialization::MetadataJsonParser meta_parser;
        auto chunks = meta_parser.parse(msg.body);
        if (!chunks)
            return common::Result<std::vector<IncomingMessage>>::fail(
                common::Error{common::ErrorCode::protocol_error,
                              "audio.metadata parse error",
                              chunks.error().what()});

        // Reference: raises UnexpectedResponse("No WordBoundary metadata found")
        // when all entries are SessionEnd → treat as protocol_error.
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

    // Reference: path == b"turn.end"
    if (*path == "turn.end")
        return common::Result<std::vector<IncomingMessage>>::ok(
            {{IncomingMessage{IncomingMessageKind::turn_end, std::nullopt}}});

    // Reference: path not in (b"response", b"turn.start") → UnknownResponse
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
    // Reference: communicate.py __stream() BINARY branch
    //
    // Binary frame format:
    //   bytes 0-1          : big-endian uint16 header_length
    //                        (includes these 2 bytes, excludes \r\n separator)
    //   bytes 2..HL-1      : header content (HL-2 bytes)
    //   bytes HL..HL+1     : \r\n separator
    //   bytes HL+2..end    : body (audio data)

    // Reference: if len(received.data) < 2 → UnexpectedResponse
    if (data.size() < 2)
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary message too short for header length"});

    const auto header_length = static_cast<std::size_t>(
        (static_cast<uint8_t>(data[0]) << 8) | static_cast<uint8_t>(data[1]));

    // Stricter than reference: header_length encodes the 2-byte prefix plus the
    // header content, so the minimum meaningful value is 2.  Values less than 2
    // indicate a malformed frame (reference would crash with ValueError in
    // get_headers_and_data since an empty line has no ':').
    if (header_length < 2)
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary frame header_length is too small (minimum 2)"});

    // Reference: if header_length > len(received.data) → UnexpectedResponse
    if (header_length > data.size())
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary message header_length exceeds message size"});

    // Stricter than reference: verify that the \r\n separator bytes after the
    // header content are actually present and correct.  The Python reference
    // uses data[header_length + 2:] for the body without checking those bytes;
    // a truncated frame (header_length + 2 > data.size()) would silently yield
    // an empty body.  We reject it explicitly.
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

    // Reference: if parameters.get(b"Path") != b"audio" → UnexpectedResponse
    const auto path = headers.header("Path");
    if (!path || *path != "audio")
        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary frame path is not audio",
                          path.value_or("(absent)")});

    // Reference: content_type = parameters.get(b"Content-Type", None)
    //            if content_type not in (b"audio/mpeg", None) → UnexpectedResponse
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
        // Reference: if content_type is None and len(data) == 0 → continue (ignore)
        //            if content_type is None and len(data) > 0  → UnexpectedResponse
        if (body_len == 0)
            return common::Result<std::vector<IncomingMessage>>::ok(
                {{IncomingMessage{IncomingMessageKind::ignored, std::nullopt}}});

        return common::Result<std::vector<IncomingMessage>>::fail(
            common::Error{common::ErrorCode::protocol_error,
                          "binary message has no Content-Type but non-empty body"});
    }

    // content_type == "audio/mpeg"
    // Reference: if len(data) == 0 → UnexpectedResponse
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
