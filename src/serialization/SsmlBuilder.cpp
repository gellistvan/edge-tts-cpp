#include "edge_tts/serialization/SsmlBuilder.hpp"
#include "edge_tts/serialization/TextNormalizer.hpp"
#include "edge_tts/serialization/XmlEscaper.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/common/Error.hpp"

#include <string>

namespace edge_tts::serialization {

common::Result<std::string> SsmlBuilder::build(
    const core::TtsConfig& config,
    std::string_view       raw_text) const
{
    // --- 1. Validate config ---------------------------------------------------
    // validate_tts_config accepts both short and full voice forms.
    auto validation = core::validate_tts_config(config);
    if (!validation)
        return common::Result<std::string>::fail(validation.error());

    // --- 2. Normalise voice name to the full form the service expects ---------
    // The service always requires the full "Microsoft Server Speech Text to
    // Speech Voice (locale, name)" form.  normalize_voice_name returns nullopt
    // only for completely invalid names, which validate_tts_config already
    // rejected above, so the dereference is safe.
    const std::string voice = *core::normalize_voice_name(config.voice);

    // --- 3. Normalize raw text (UTF-8 validation + control-char replacement) --
    TextNormalizer normalizer;
    auto norm = normalizer.normalize(raw_text);
    if (!norm)
        return common::Result<std::string>::fail(norm.error());

    // --- 4. XML-escape the normalized text exactly once ----------------------
    const std::string escaped = xml_escape(*norm);

    // --- 5. Build SSML --------------------------------------------------------
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
    std::string ssml;
    ssml.reserve(256 + escaped.size());
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
    ssml += escaped;
    ssml += "</prosody>";
    ssml += "</voice>";
    ssml += "</speak>";

    return common::Result<std::string>::ok(std::move(ssml));
}

} // namespace edge_tts::serialization
