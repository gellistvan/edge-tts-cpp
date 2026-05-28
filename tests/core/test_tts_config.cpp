#include "edge_tts/core/TtsConfig.hpp"

#ifndef EDGE_TTS_NO_GTEST
#include <gtest/gtest.h>

TEST(TtsConfig, DefaultConfigIsValid) {
    edge_tts::core::TtsConfig config;
    EXPECT_NO_THROW(config.validate());
}
#endif
