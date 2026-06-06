#include "edge_tts/serialization/SsmlBuilder.hpp"
#include "edge_tts/serialization/TextNormalizer.hpp"
#include "edge_tts/serialization/XmlEscaper.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/common/Error.hpp"

#include <string>

namespace edge_tts::serialization {

// Shared SSML assembly for both build() and build_from_escaped_text().
// Precondition: voice is already the full normalized form; escaped_content is
// already XML-safe (either freshly escaped by build(), or pre-escaped by the
// caller in build_from_escaped_text()).
//
// Template mirrors Python communicate.py mkssml() exactly:
//   <speak version='1.0' xmlns='...' xml:lang='en-US'>
//   <voice name='{voice}'>
//   <prosody pitch='{pitch}' rate='{rate}' volume='{volume}'>
//   {escaped_text}
//   </prosody></voice></speak>
//
// Notes:
//   - Single-quoted attribute values (matches Python f-string literals).
//   - xml:lang is hardcoded 'en-US' regardless of the voice locale.
//   - Prosody attribute order: pitch, rate, volume (reference order).
//   - All on one line — no inter-element whitespace.
static std::string assemble_ssml(const std::string&     voice,
                                  const core::TtsConfig& config,
                                  std::string_view       escaped_content)
{
    std::string ssml;
    ssml.reserve(256 + escaped_content.size());
    ssml += "<speak version='1.0'"
            " xmlns='http://www.w3.org/2001/10/synthesis'"
            " xml:lang='en-US'>";
    ssml += "<voice name='";
    ssml += voice;
    ssml += "'>";
    ssml += "<prosody pitch='";
    ssml += config.pitch;
    ssml += "' rate='";
    ssml += config.rate;
    ssml += "' volume='";
    ssml += config.volume;
    ssml += "'>";
    ssml += escaped_content;
    ssml += "</prosody>";
    ssml += "</voice>";
    ssml += "</speak>";
    return ssml;
}

// Shared validation + voice normalization step for both entry points.
// Returns the normalized full voice name, or a Result failure.
static common::Result<std::string> validate_and_normalize_voice(
    const core::TtsConfig& config)
{
    // validate_tts_config accepts both short and full voice forms.
    auto validation = core::validate_tts_config(config);
    if (!validation)
        return common::Result<std::string>::fail(validation.error());

    // normalize_voice_name returns nullopt only for completely invalid names,
    // which validate_tts_config already rejected above.
    return common::Result<std::string>::ok(
        *core::normalize_voice_name(config.voice));
}

common::Result<std::string> SsmlBuilder::build(
    const core::TtsConfig& config,
    std::string_view       raw_text) const
{
    auto voice = validate_and_normalize_voice(config);
    if (!voice)
        return common::Result<std::string>::fail(voice.error());

    // Normalize raw text (UTF-8 validation + control-char replacement).
    TextNormalizer normalizer;
    auto norm = normalizer.normalize(raw_text);
    if (!norm)
        return common::Result<std::string>::fail(norm.error());

    // XML-escape the normalized text exactly once.
    const std::string escaped = xml_escape(*norm);

    return common::Result<std::string>::ok(
        assemble_ssml(*voice, config, escaped));
}

common::Result<std::string> SsmlBuilder::build_from_escaped_text(
    const core::TtsConfig& config,
    std::string_view       escaped_text) const
{
    auto voice = validate_and_normalize_voice(config);
    if (!voice)
        return common::Result<std::string>::fail(voice.error());

    // escaped_text is embedded verbatim — no normalize, no xml_escape.
    return common::Result<std::string>::ok(
        assemble_ssml(*voice, config, escaped_text));
}

} // namespace edge_tts::serialization
