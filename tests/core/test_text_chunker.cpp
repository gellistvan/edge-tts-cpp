#include "edge_tts/core/TextChunker.hpp"
#include "vendor/minigtest/minigtest.hpp"

using edge_tts::core::TextChunker;

TEST(TextChunker, SplitsAsciiIntoChunks) {
    TextChunker chunker{3};
    const auto chunks = chunker.split("abcdefg");
    ASSERT_EQ(chunks.size(), 3u);
    EXPECT_EQ(chunks[0], "abc");
    EXPECT_EQ(chunks[1], "def");
    EXPECT_EQ(chunks[2], "g");
}

TEST(TextChunker, EmptyTextReturnsEmpty) {
    TextChunker chunker{8};
    EXPECT_TRUE(chunker.split("").empty());
}

TEST(TextChunker, TextSmallerThanChunkIsOneChunk) {
    TextChunker chunker{100};
    const auto chunks = chunker.split("hello");
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0], "hello");
}

TEST(TextChunker, ZeroMaxBytesThrows) {
    EXPECT_THROW(TextChunker{0}, std::invalid_argument);
}

TEST(TextChunker, DoesNotSplitAsciiMidCharacter) {
    // "abc" in 4-byte chunks → one chunk containing all 3 bytes
    TextChunker chunker{4};
    const auto chunks = chunker.split("abcde");
    ASSERT_EQ(chunks.size(), 2u);
    EXPECT_EQ(chunks[0], "abcd");
    EXPECT_EQ(chunks[1], "e");
}
