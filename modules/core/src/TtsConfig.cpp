#include "core/TtsConfig.hpp"
#include "common/Error.hpp"
#include "common/Errors.hpp"

#include <regex>

namespace edge_tts::core {

// ---------------------------------------------------------------------------
// BoundaryType helpers
// ---------------------------------------------------------------------------

std::string_view to_string(BoundaryType type) noexcept {
    switch (type) {
        case BoundaryType::word:     return "WordBoundary";
        case BoundaryType::sentence: return "SentenceBoundary";
    }
    return "SentenceBoundary";  // unreachable; keeps compiler happy
}

common::Result<BoundaryType> boundary_type_from_string(std::string_view value) {
    if (value == "WordBoundary")     return common::Result<BoundaryType>::ok(BoundaryType::word);
    if (value == "SentenceBoundary") return common::Result<BoundaryType>::ok(BoundaryType::sentence);
    return common::Result<BoundaryType>::fail(
        common::Error{common::ErrorCode::invalid_argument,
                      "boundary_type_from_string: unknown value",
                      std::string{value}});
}

// ---------------------------------------------------------------------------
// TtsConfig
// ---------------------------------------------------------------------------

TtsConfig TtsConfig::defaults() {
    return TtsConfig{};  // all default member initialisers are the reference defaults
}

// ---------------------------------------------------------------------------
// Voice normalisation
// ---------------------------------------------------------------------------

namespace {

const std::regex& full_voice_re() {
    static const std::regex re{
        R"(^Microsoft Server Speech Text to Speech Voice \(.+, .+\)$)"};
    return re;
}

const std::regex& short_voice_re() {
    static const std::regex re{R"(^([a-z]{2,})-([A-Z]{2,})-(.+Neural)$)"};
    return re;
}

} // namespace

std::optional<std::string> normalize_voice_name(std::string_view voice_sv) {
    const std::string voice{voice_sv};

    if (std::regex_match(voice, full_voice_re())) return voice;

    std::smatch m;
    if (!std::regex_match(voice, m, short_voice_re())) return std::nullopt;

    std::string lang   = m[1].str();
    std::string region = m[2].str();
    std::string name   = m[3].str();

    const auto dash = name.find('-');
    if (dash != std::string::npos) {
        region += '-' + name.substr(0, dash);
        name    = name.substr(dash + 1);
    }

    return "Microsoft Server Speech Text to Speech Voice"
           " (" + lang + '-' + region + ", " + name + ')';
}

// ---------------------------------------------------------------------------
// TtsConfig::validate() — legacy throw-based bridge
// ---------------------------------------------------------------------------

void TtsConfig::validate() {
    auto normalized = normalize_voice_name(voice);
    if (!normalized) {
        throw common::ConfigurationError{"Invalid voice '" + voice + "'"};
    }
    voice = std::move(*normalized);

    auto result = validate_tts_config(*this);
    if (!result.has_value()) {
        throw common::ConfigurationError{std::string{result.error().message()}
                                         + ": " + std::string{result.error().context()}};
    }
}

// ---------------------------------------------------------------------------
// validate_tts_config
// ---------------------------------------------------------------------------

common::Result<void> validate_tts_config(const TtsConfig& config) {
    static const std::regex rate_re  {R"(^[+-]\d+%$)"};
    static const std::regex volume_re{R"(^[+-]\d+%$)"};
    static const std::regex pitch_re {R"(^[+-]\d+Hz$)"};

    // Voice — accept short or full form; nullopt means neither
    if (!normalize_voice_name(config.voice)) {
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::invalid_argument,
                          "voice: invalid value",
                          config.voice});
    }

    // Rate
    if (!std::regex_match(config.rate, rate_re)) {
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::invalid_argument,
                          "rate: invalid value",
                          config.rate});
    }

    // Volume
    if (!std::regex_match(config.volume, volume_re)) {
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::invalid_argument,
                          "volume: invalid value",
                          config.volume});
    }

    // Pitch
    if (!std::regex_match(config.pitch, pitch_re)) {
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::invalid_argument,
                          "pitch: invalid value",
                          config.pitch});
    }

    return common::Result<void>::ok();
}

} // namespace edge_tts::core
