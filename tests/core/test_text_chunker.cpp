#include "edge_tts/core/TextChunker.hpp"

#ifndef EDGE_TTS_NO_GTEST
#include <gtest/gtest.h>

TEST(TextChunker, SplitsTextIntoChunks) {
    edge_tts::core::TextChunker chunker{3};
    const auto chunks = chunker.split("abcdefg");
    ASSERT_EQ(chunks.size(), 3U);
    EXPECT_EQ(chunks[0], "abc");
    EXPECT_EQ(chunks[1], "def");
    EXPECT_EQ(chunks[2], "g");
}
#endif
