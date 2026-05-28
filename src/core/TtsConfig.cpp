#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/common/Errors.hpp"

#include <regex>

namespace edge_tts::core {

namespace {

// Matches the full "Microsoft Server Speech..." voice name expected on the wire.
const std::regex& full_voice_re() {
    static const std::regex re{
        R"(^Microsoft Server Speech Text to Speech Voice \(.+, .+\)$)"};
    return re;
}

// Matches a short voice name like "en-US-EmmaMultilingualNeural".
// Capture groups: (1) language, (2) region, (3) name-suffix ending in "Neural".
const std::regex& short_voice_re() {
    static const std::regex re{R"(^([a-z]{2,})-([A-Z]{2,})-(.+Neural)$)"};
    return re;
}

void check_param(const std::string& name, const std::string& value,
                 const std::regex& pattern) {
    if (!std::regex_match(value, pattern)) {
        throw common::ConfigurationError{"Invalid " + name + " '" + value + "'"};
    }
}

} // namespace

std::optional<std::string> normalize_voice_name(std::string_view voice_sv) {
    const std::string voice{voice_sv};

    // Already in full form — accept as-is.
    if (std::regex_match(voice, full_voice_re())) return voice;

    // Try the short "lang-REGION-NameNeural" form.
    std::smatch m;
    if (!std::regex_match(voice, m, short_voice_re())) return std::nullopt;

    std::string lang   = m[1].str();
    std::string region = m[2].str();
    std::string name   = m[3].str();

    // A compound region like "AndrewMultilingual-CasualNeural":
    //   region → "US-AndrewMultilingual", name → "CasualNeural"
    const auto dash = name.find('-');
    if (dash != std::string::npos) {
        region += '-' + name.substr(0, dash);
        name    = name.substr(dash + 1);
    }

    return "Microsoft Server Speech Text to Speech Voice"
           " (" + lang + '-' + region + ", " + name + ')';
}

void TtsConfig::validate() {
    static const std::regex rate_re{R"(^[+-]\d+%$)"};
    static const std::regex volume_re{R"(^[+-]\d+%$)"};
    static const std::regex pitch_re{R"(^[+-]\d+Hz$)"};

    auto normalized = normalize_voice_name(voice);
    if (!normalized) {
        throw common::ConfigurationError{"Invalid voice '" + voice + "'"};
    }
    voice = std::move(*normalized);

    check_param("rate",   rate,   rate_re);
    check_param("volume", volume, volume_re);
    check_param("pitch",  pitch,  pitch_re);
}

} // namespace edge_tts::core
