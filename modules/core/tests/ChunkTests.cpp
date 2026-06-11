#include "core/Chunk.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstddef>
#include <utility>
#include <vector>

using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::BoundaryEventType;
using edge_tts::core::TtsChunk;
using edge_tts::core::is_audio;
using edge_tts::core::is_boundary;

// ---------------------------------------------------------------------------
// AudioChunk
// ---------------------------------------------------------------------------

TEST(AudioChunk, StoresExactBytes) {
    const std::vector<std::byte> payload{
        std::byte{0xFF}, std::byte{0xFB}, std::byte{0x90}, std::byte{0x00}};
    AudioChunk chunk{payload};
    EXPECT_EQ(chunk.data.size(), 4u);
    EXPECT_EQ(chunk.data[0], std::byte{0xFF});
    EXPECT_EQ(chunk.data[1], std::byte{0xFB});
    EXPECT_EQ(chunk.data[2], std::byte{0x90});
    EXPECT_EQ(chunk.data[3], std::byte{0x00});
}

TEST(AudioChunk, EmptyDataIsValid) {
    AudioChunk chunk;
    EXPECT_TRUE(chunk.data.empty());
}

TEST(AudioChunk, EqualityOnSameData) {
    const std::vector<std::byte> d{std::byte{0xAB}, std::byte{0xCD}};
    EXPECT_TRUE(AudioChunk{d} == AudioChunk{d});
    EXPECT_FALSE(AudioChunk{d} != AudioChunk{d});
}

TEST(AudioChunk, InequalityOnDifferentData) {
    const std::vector<std::byte> d1{std::byte{0x01}};
    const std::vector<std::byte> d2{std::byte{0x02}};
    EXPECT_TRUE(AudioChunk{d1} != AudioChunk{d2});
}

TEST(AudioChunk, MoveSemantics) {
    // Moving a large audio chunk must not copy.
    std::vector<std::byte> large(100'000, std::byte{0x55});
    const auto* original_ptr = large.data();
    AudioChunk chunk;
    chunk.data = std::move(large);
    EXPECT_EQ(chunk.data.data(), original_ptr);  // pointer unchanged — move happened
    EXPECT_EQ(chunk.data.size(), 100'000u);
}

TEST(AudioChunk, MoveChunkItself) {
    AudioChunk src{{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};
    const auto* ptr = src.data.data();
    AudioChunk dst{std::move(src)};
    EXPECT_EQ(dst.data.data(), ptr);
    EXPECT_EQ(dst.data.size(), 3u);
    EXPECT_TRUE(src.data.empty());
}

// ---------------------------------------------------------------------------
// BoundaryChunk
// ---------------------------------------------------------------------------

TEST(BoundaryChunk, StoresText) {
    BoundaryChunk chunk;
    chunk.text = "Hello, world!";
    EXPECT_EQ(chunk.text, "Hello, world!");
}

TEST(BoundaryChunk, StoresOffset) {
    BoundaryChunk chunk;
    chunk.offset_ticks = 12345678LL;
    EXPECT_EQ(chunk.offset_ticks, 12345678LL);
}

TEST(BoundaryChunk, StoresDuration) {
    BoundaryChunk chunk;
    chunk.duration_ticks = 9876543LL;
    EXPECT_EQ(chunk.duration_ticks, 9876543LL);
}

TEST(BoundaryChunk, DefaultTypeIsSentenceBoundary) {
    BoundaryChunk chunk;
    EXPECT_TRUE(chunk.type == BoundaryEventType::SentenceBoundary);
}

TEST(BoundaryChunk, WordBoundaryType) {
    BoundaryChunk chunk;
    chunk.type = BoundaryEventType::WordBoundary;
    EXPECT_TRUE(chunk.type == BoundaryEventType::WordBoundary);
}

TEST(BoundaryChunk, ZeroDurationIsValid) {
    // A boundary event with duration = 0 is valid (e.g. sentence boundaries
    // sometimes have zero duration in the reference stream).
    BoundaryChunk chunk;
    chunk.text           = "End.";
    chunk.offset_ticks   = 50'000'000LL;  // 5 seconds
    chunk.duration_ticks = 0LL;
    EXPECT_EQ(chunk.duration_ticks, 0LL);
    EXPECT_EQ(chunk.text, "End.");
}

TEST(BoundaryChunk, TicksToMicroseconds) {
    // 1 tick = 100 ns = 0.1 µs, so 10 ticks = 1 µs
    BoundaryChunk chunk;
    chunk.offset_ticks   = 10'000'000LL;  // 1 second = 10_000_000 ticks
    chunk.duration_ticks =  5'000'000LL;  // 0.5 second
    const auto start_us  = chunk.offset_ticks / 10;
    const auto end_us    = (chunk.offset_ticks + chunk.duration_ticks) / 10;
    EXPECT_EQ(start_us, 1'000'000LL);   // 1 s = 1_000_000 µs
    EXPECT_EQ(end_us,   1'500'000LL);   // 1.5 s
}

TEST(BoundaryChunk, Equality) {
    BoundaryChunk a;
    a.type = BoundaryEventType::WordBoundary;
    a.text = "hello"; a.offset_ticks = 100; a.duration_ticks = 50;
    BoundaryChunk b = a;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(BoundaryChunk, InequalityOnText) {
    BoundaryChunk a, b;
    a.text = "hello"; b.text = "world";
    EXPECT_TRUE(a != b);
}

TEST(BoundaryChunk, InequalityOnOffset) {
    BoundaryChunk a, b;
    a.offset_ticks = 100; b.offset_ticks = 200;
    EXPECT_TRUE(a != b);
}

// ---------------------------------------------------------------------------
// TtsChunk variant helpers
// ---------------------------------------------------------------------------

TEST(TtsChunk, IsAudioForAudioChunk) {
    TtsChunk c = AudioChunk{{std::byte{0x01}}};
    EXPECT_TRUE(is_audio(c));
    EXPECT_FALSE(is_boundary(c));
}

TEST(TtsChunk, IsBoundaryForBoundaryChunk) {
    BoundaryChunk bc;
    bc.text = "test";
    TtsChunk c = bc;
    EXPECT_TRUE(is_boundary(c));
    EXPECT_FALSE(is_audio(c));
}

TEST(TtsChunk, CanHoldAudioViaMove) {
    std::vector<std::byte> payload(1000, std::byte{0xAA});
    const auto* ptr = payload.data();
    TtsChunk c = AudioChunk{std::move(payload)};
    EXPECT_TRUE(is_audio(c));
    EXPECT_EQ(std::get<AudioChunk>(c).data.data(), ptr);
}

TEST(TtsChunk, AccessWordBoundaryFields) {
    BoundaryChunk bc;
    bc.type           = BoundaryEventType::WordBoundary;
    bc.text           = "synthesize";
    bc.offset_ticks   = 1'000'000LL;
    bc.duration_ticks = 500'000LL;
    TtsChunk c = bc;

    EXPECT_TRUE(is_boundary(c));
    const auto& got = std::get<BoundaryChunk>(c);
    EXPECT_TRUE(got.type == BoundaryEventType::WordBoundary);
    EXPECT_EQ(got.text,           "synthesize");
    EXPECT_EQ(got.offset_ticks,   1'000'000LL);
    EXPECT_EQ(got.duration_ticks,   500'000LL);
}
