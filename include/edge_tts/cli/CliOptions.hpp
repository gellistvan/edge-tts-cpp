#pragma once

#include <optional>
#include <string>

namespace edge_tts::cli {

// Legacy shared options type; superseded by EdgeTtsArguments and PlaybackArguments.
// Retained for backward compatibility.
struct CliOptions {
    std::string              text;
    std::optional<std::string> file;
    std::string              voice{"en-US-EmmaMultilingualNeural"};
    std::string              rate{"+0%"};
    std::string              volume{"+0%"};
    std::string              pitch{"+0Hz"};
    std::optional<std::string> write_media;
    std::optional<std::string> write_subtitles;
    std::optional<std::string> proxy;
    bool                     list_voices{false};
};

} // namespace edge_tts::cli
