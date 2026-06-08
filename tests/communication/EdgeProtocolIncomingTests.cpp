#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/communication/IncomingMessage.hpp"
#include "edge_tts/communication/WebSocketMessage.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "communication/WebSocketFrameHelpers.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::IncomingMessage;
using edge_tts::communication::IncomingMessageKind;
using edge_tts::communication::WebSocketMessage;
using edge_tts::common::SystemClock;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::BoundaryEventType;
using edge_tts::core::TtsChunk;
using edge_tts::test::to_bytes;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static SystemClock g_clock;
static EdgeProtocol proto{g_clock};

// Build a text WebSocketMessage
static WebSocketMessage text_msg(const std::string& text) {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = text;
    return m;
}

// Build a binary WebSocketMessage from raw bytes
static WebSocketMessage binary_msg(std::vector<std::byte> bytes) {
    WebSocketMessage m;
    m.type   = WebSocketMessage::Type::binary;
    m.binary = std::move(bytes);
    return m;
}

// Build a well-formed binary audio frame.
//
// Binary frame format (from communicate.py analysis):
//   bytes 0-1            : header_length (big-endian uint16)
//                          = 2 (prefix) + len(header_content_without_trailing_crlf)
//   bytes 2..HL-1        : header content (no trailing \r\n)
//   bytes HL..HL+1       : \r\n separator
//   bytes HL+2..end      : body (audio data)
//
// The header_content string should be in "Key:Value\r\nKey:Value" format
// (with \r\n between headers but NOT at the end).
static WebSocketMessage make_audio_frame(
    const std::string&        header_content,
    const std::vector<std::byte>& body)
{
    const auto hl = static_cast<uint16_t>(2 + header_content.size());
    std::vector<std::byte> frame;
    frame.reserve(2 + header_content.size() + 2 + body.size());
    frame.push_back(static_cast<std::byte>(hl >> 8));
    frame.push_back(static_cast<std::byte>(hl & 0xff));
    for (char c : header_content)
        frame.push_back(static_cast<std::byte>(c));
    frame.push_back(static_cast<std::byte>('\r'));
    frame.push_back(static_cast<std::byte>('\n'));
    for (auto b : body)
        frame.push_back(b);
    return binary_msg(std::move(frame));
}

// Standard audio frame with Path:audio + Content-Type:audio/mpeg
static WebSocketMessage standard_audio_frame(const std::vector<std::byte>& body) {
    return make_audio_frame(
        "X-RequestId:abc123\r\nPath:audio\r\nContent-Type:audio/mpeg",
        body);
}

// ---------------------------------------------------------------------------
// Binary audio frame → audio chunk
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, BinaryAudioFrameYieldsAudioKind) {
    const auto body = to_bytes("AUDIOBYTES");
    const auto result = proto.parse_incoming(standard_audio_frame(body));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].kind, IncomingMessageKind::audio);
}

TEST(EdgeProtocolIncoming, BinaryAudioFrameChunkIsPresent) {
    const auto body = to_bytes("AUDIOBYTES");
    const auto result = proto.parse_incoming(standard_audio_frame(body));
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE((*result)[0].chunk.has_value());
    EXPECT_TRUE(std::holds_alternative<AudioChunk>(*(*result)[0].chunk));
}

// ---------------------------------------------------------------------------
// Audio bytes are preserved exactly
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, AudioBytesPreservedExactly) {
    const std::vector<std::byte> body = {
        std::byte{0xff}, std::byte{0xfb}, std::byte{0x90}, std::byte{0x00},
        std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef},
    };
    const auto result = proto.parse_incoming(standard_audio_frame(body));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_TRUE((*result)[0].chunk.has_value());
    const auto& audio = std::get<AudioChunk>(*(*result)[0].chunk);
    EXPECT_EQ(audio.data, body);
}

// ---------------------------------------------------------------------------
// Word boundary metadata frame
// ---------------------------------------------------------------------------

static const std::string kWordBoundaryMetaFrame =
    "X-RequestId:abc\r\nPath:audio.metadata\r\n\r\n"
    R"({"Metadata":[{"Type":"WordBoundary","Data":{"Offset":100,"Duration":200,"text":{"Text":"hello"}}}]})";

TEST(EdgeProtocolIncoming, WordBoundaryYieldsBoundaryKind) {
    const auto result = proto.parse_incoming(text_msg(kWordBoundaryMetaFrame));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].kind, IncomingMessageKind::boundary);
}

TEST(EdgeProtocolIncoming, WordBoundaryChunkFields) {
    const auto result = proto.parse_incoming(text_msg(kWordBoundaryMetaFrame));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_TRUE((*result)[0].chunk.has_value());
    const auto& bc = std::get<BoundaryChunk>(*(*result)[0].chunk);
    EXPECT_EQ(bc.type, BoundaryEventType::WordBoundary);
    EXPECT_EQ(bc.text, "hello");
    EXPECT_EQ(bc.offset_ticks, 100);
    EXPECT_EQ(bc.duration_ticks, 200);
}

// ---------------------------------------------------------------------------
// Sentence boundary metadata frame
// ---------------------------------------------------------------------------

static const std::string kSentenceBoundaryMetaFrame =
    "X-RequestId:abc\r\nPath:audio.metadata\r\n\r\n"
    R"({"Metadata":[{"Type":"SentenceBoundary","Data":{"Offset":500,"Duration":1000,"text":{"Text":"world."}}}]})";

TEST(EdgeProtocolIncoming, SentenceBoundaryYieldsBoundaryKind) {
    const auto result = proto.parse_incoming(text_msg(kSentenceBoundaryMetaFrame));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].kind, IncomingMessageKind::boundary);
}

TEST(EdgeProtocolIncoming, SentenceBoundaryEventType) {
    const auto result = proto.parse_incoming(text_msg(kSentenceBoundaryMetaFrame));
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE((*result)[0].chunk.has_value());
    const auto& bc = std::get<BoundaryChunk>(*(*result)[0].chunk);
    EXPECT_EQ(bc.type, BoundaryEventType::SentenceBoundary);
}

// ---------------------------------------------------------------------------
// Multiple boundary events in one metadata frame
// ---------------------------------------------------------------------------

static const std::string kMultiBoundaryMetaFrame =
    "X-RequestId:abc\r\nPath:audio.metadata\r\n\r\n"
    R"({"Metadata":[)"
    R"({"Type":"WordBoundary","Data":{"Offset":10,"Duration":20,"text":{"Text":"foo"}}},)"
    R"({"Type":"WordBoundary","Data":{"Offset":30,"Duration":40,"text":{"Text":"bar"}}})"
    R"(]})";

TEST(EdgeProtocolIncoming, MultipleBoundariesYieldMultipleMessages) {
    const auto result = proto.parse_incoming(text_msg(kMultiBoundaryMetaFrame));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 2u);
    EXPECT_EQ((*result)[0].kind, IncomingMessageKind::boundary);
    EXPECT_EQ((*result)[1].kind, IncomingMessageKind::boundary);
}

TEST(EdgeProtocolIncoming, MultipleBoundariesHaveCorrectText) {
    const auto result = proto.parse_incoming(text_msg(kMultiBoundaryMetaFrame));
    EXPECT_TRUE(result.has_value());
    const auto& bc0 = std::get<BoundaryChunk>(*(*result)[0].chunk);
    const auto& bc1 = std::get<BoundaryChunk>(*(*result)[1].chunk);
    EXPECT_EQ(bc0.text, "foo");
    EXPECT_EQ(bc1.text, "bar");
}

// ---------------------------------------------------------------------------
// Turn end frame
// ---------------------------------------------------------------------------

static const std::string kTurnEndFrame =
    "X-RequestId:abc\r\nPath:turn.end\r\n\r\n";

TEST(EdgeProtocolIncoming, TurnEndYieldsTurnEndKind) {
    const auto result = proto.parse_incoming(text_msg(kTurnEndFrame));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].kind, IncomingMessageKind::turn_end);
}

TEST(EdgeProtocolIncoming, TurnEndChunkIsAbsent) {
    const auto result = proto.parse_incoming(text_msg(kTurnEndFrame));
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE((*result)[0].chunk.has_value());
}

// ---------------------------------------------------------------------------
// Ignored paths: response and turn.start
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, ResponsePathYieldsIgnored) {
    const std::string frame = "X-RequestId:abc\r\nPath:response\r\n\r\n{}";
    const auto result = proto.parse_incoming(text_msg(frame));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].kind, IncomingMessageKind::ignored);
}

TEST(EdgeProtocolIncoming, TurnStartPathYieldsIgnored) {
    const std::string frame = "X-RequestId:abc\r\nPath:turn.start\r\n\r\n";
    const auto result = proto.parse_incoming(text_msg(frame));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].kind, IncomingMessageKind::ignored);
}

TEST(EdgeProtocolIncoming, IgnoredMessageHasNoChunk) {
    const std::string frame = "X-RequestId:abc\r\nPath:response\r\n\r\n";
    const auto result = proto.parse_incoming(text_msg(frame));
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE((*result)[0].chunk.has_value());
}

// ---------------------------------------------------------------------------
// Unknown text frame path → protocol_error
// Reference: raise UnknownResponse("Unknown path received")
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, UnknownTextPathIsError) {
    const std::string frame = "X-RequestId:abc\r\nPath:unknown.path\r\n\r\n";
    const auto result = proto.parse_incoming(text_msg(frame));
    EXPECT_FALSE(result.has_value());
}

TEST(EdgeProtocolIncoming, MissingPathHeaderIsError) {
    const std::string frame = "X-RequestId:abc\r\n\r\n";
    const auto result = proto.parse_incoming(text_msg(frame));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Malformed text frame (no \r\n\r\n separator)
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, MalformedTextFrameIsError) {
    const std::string frame = "Path:audio.metadataXXXXno-separator";
    const auto result = proto.parse_incoming(text_msg(frame));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Malformed metadata JSON
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, MalformedMetadataJsonIsError) {
    const std::string frame =
        "X-RequestId:abc\r\nPath:audio.metadata\r\n\r\nnot-valid-json";
    const auto result = proto.parse_incoming(text_msg(frame));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Metadata with only SessionEnd entries → protocol_error
// Reference: "No WordBoundary metadata found"
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, AllSessionEndMetadataIsError) {
    const std::string frame =
        "X-RequestId:abc\r\nPath:audio.metadata\r\n\r\n"
        R"({"Metadata":[{"Type":"SessionEnd","Data":{"Offset":0,"Duration":0,"text":{"Text":""}}}]})";
    const auto result = proto.parse_incoming(text_msg(frame));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Empty binary message → protocol_error (too short for header length)
// Reference: "We received a binary message, but it is missing the header length."
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, EmptyBinaryIsError) {
    const auto result = proto.parse_incoming(binary_msg({}));
    EXPECT_FALSE(result.has_value());
}

TEST(EdgeProtocolIncoming, OneByteBinaryIsError) {
    const auto result = proto.parse_incoming(binary_msg({std::byte{0x00}}));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Binary frame: header_length exceeds message size → error
// Reference: "The header length is greater than the length of the data."
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, HeaderLengthExceedsSizeIsError) {
    // header_length = 0x00FF (255) but total message is only 2 bytes
    std::vector<std::byte> frame = {std::byte{0x00}, std::byte{0xff}};
    const auto result = proto.parse_incoming(binary_msg(std::move(frame)));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Binary frame: Path != audio → error
// Reference: "Received binary message, but the path is not audio."
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, BinaryNonAudioPathIsError) {
    const auto result = proto.parse_incoming(
        make_audio_frame("X-RequestId:abc\r\nPath:ssml\r\nContent-Type:audio/mpeg",
                         to_bytes("data")));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Binary frame: unexpected Content-Type → error
// Reference: "Received binary message, but with an unexpected Content-Type."
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, UnexpectedContentTypeIsError) {
    const auto result = proto.parse_incoming(
        make_audio_frame("X-RequestId:abc\r\nPath:audio\r\nContent-Type:text/plain",
                         to_bytes("data")));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Binary frame: no Content-Type, empty body → ignored
// Reference: continue (silently skip)
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, NoContentTypeEmptyBodyYieldsIgnored) {
    const auto result = proto.parse_incoming(
        make_audio_frame("X-RequestId:abc\r\nPath:audio", {}));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].kind, IncomingMessageKind::ignored);
}

// ---------------------------------------------------------------------------
// Binary frame: no Content-Type, non-empty body → error
// Reference: "Received binary message with no Content-Type, but with data."
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, NoContentTypeNonEmptyBodyIsError) {
    const auto result = proto.parse_incoming(
        make_audio_frame("X-RequestId:abc\r\nPath:audio", to_bytes("DATA")));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Binary frame: audio/mpeg Content-Type, empty body → error
// Reference: "Received binary message, but it is missing the audio data."
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, AudioMpegEmptyBodyIsError) {
    const auto result = proto.parse_incoming(
        make_audio_frame("X-RequestId:abc\r\nPath:audio\r\nContent-Type:audio/mpeg", {}));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Binary frame: exactly the boundary case — header_length + 2 == data.size()
// Separator is present, body is empty.
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, BodyOffsetBeyondEndTreatedAsEmpty) {
    // make_audio_frame always appends the \r\n separator.  With an empty body
    // argument: header_length + 2 == data.size() — separator present, body empty.
    // Path:audio, no Content-Type → should yield ignored (empty body + no CT).
    const auto msg = make_audio_frame("X-RequestId:abc\r\nPath:audio", {});
    const auto result = proto.parse_incoming(msg);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].kind, IncomingMessageKind::ignored);
}

// ---------------------------------------------------------------------------
// Binary frame hardening: header_length too small
//
// header_length encodes the 2-byte prefix + header content, so minimum is 2.
// Values 0 and 1 are malformed — the reference crashes with a ValueError in
// get_headers_and_data; C++ returns protocol_error deterministically.
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, BinaryHeaderLengthZeroIsError) {
    // header_length = 0 (impossible: even an empty header needs the 2-byte prefix)
    std::vector<std::byte> frame = {
        std::byte{0x00}, std::byte{0x00},   // header_length = 0
        std::byte{0xAB}, std::byte{0xCD},   // more bytes follow (irrelevant)
    };
    const auto result = proto.parse_incoming(binary_msg(std::move(frame)));
    EXPECT_FALSE(result.has_value());
}

TEST(EdgeProtocolIncoming, BinaryHeaderLengthOneIsError) {
    // header_length = 1 — still below the 2-byte minimum
    std::vector<std::byte> frame = {
        std::byte{0x00}, std::byte{0x01},   // header_length = 1
        std::byte{0xAB}, std::byte{0xCD},
    };
    const auto result = proto.parse_incoming(binary_msg(std::move(frame)));
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Binary frame hardening: \r\n separator missing or wrong
//
// The Python reference reads data[header_length + 2:] for the body without
// verifying that the two bytes at [header_length..header_length+2) are \r\n.
// The C++ parser is stricter: both presence and correctness are required.
// ---------------------------------------------------------------------------

TEST(EdgeProtocolIncoming, BinaryMissingSeparatorIsError) {
    // Build a frame with valid header content (Path:audio) but stop before the
    // \r\n separator.  header_length == data.size() so header_length + 2 > data.size().
    const std::string hdr_content = "Path:audio";
    const uint16_t hl = static_cast<uint16_t>(2 + hdr_content.size()); // = 12
    std::vector<std::byte> frame;
    frame.push_back(static_cast<std::byte>(hl >> 8));
    frame.push_back(static_cast<std::byte>(hl & 0xff));
    for (char c : hdr_content)
        frame.push_back(static_cast<std::byte>(c));
    // Intentionally omit the \r\n separator.
    // frame.size() == header_length == 12; header_length + 2 = 14 > 12.
    const auto result = proto.parse_incoming(binary_msg(std::move(frame)));
    EXPECT_FALSE(result.has_value());
}

TEST(EdgeProtocolIncoming, BinaryWrongSeparatorBytesIsError) {
    // Build a valid audio frame, then overwrite the \r\n separator with \n\n.
    const std::string hdr = "X-RequestId:abc\r\nPath:audio\r\nContent-Type:audio/mpeg";
    auto msg = make_audio_frame(hdr, to_bytes("AUDIO"));
    // Separator bytes are at index header_length = 2 + hdr.size().
    const std::size_t sep_idx = 2 + hdr.size();
    msg.binary[sep_idx]     = std::byte{'\n'};  // wrong: should be '\r'
    msg.binary[sep_idx + 1] = std::byte{'\n'};
    const auto result = proto.parse_incoming(msg);
    EXPECT_FALSE(result.has_value());
}
