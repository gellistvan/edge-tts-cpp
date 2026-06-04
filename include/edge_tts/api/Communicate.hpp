#pragma once

#include "edge_tts/core/TtsChunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace edge_tts::api {

// Public facade for the Edge TTS text-to-speech service.
//
// Reference: communicate.py Communicate class
// Python public API surface: edge_tts/__init__.py
//
// Communicate orchestrates:
//   - text chunking       (core::TextChunker)
//   - SSML construction   (serialization::SsmlBuilder)
//   - synthesis session   (communication::SynthesisSession)
//   - subtitle generation (subtitle::SubMaker — optional)
//   - audio saving        (media)
//
// This class intentionally lives in the api module, above communication, to
// keep the communication module as pure transport infrastructure.  See
// docs/MODULES.md and docs/DEPENDENCY_RULES.md for the rationale.
//
// Behavior is not yet implemented; this is the architectural placeholder.
class Communicate final {
public:
    Communicate(std::string text, core::TtsConfig config = {});

    [[nodiscard]] const std::string&      text()   const noexcept;
    [[nodiscard]] const core::TtsConfig&  config() const noexcept;

    // Synthesize and return all TtsChunk events (audio + boundaries).
    // Reference: Communicate.stream() — returns all chunks synchronously.
    [[nodiscard]] std::vector<core::TtsChunk> stream_sync() const;

    // Synthesize and write audio (and optionally SRT subtitles) to disk.
    // Reference: Communicate.save()
    void save(const std::filesystem::path& media_path,
              const std::optional<std::filesystem::path>& subtitles_path
                  = std::nullopt) const;

private:
    std::string      text_;
    core::TtsConfig  config_;
};

} // namespace edge_tts::api
