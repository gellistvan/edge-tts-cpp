#include "core/Voice.hpp"
#include "common/Error.hpp"

namespace edge_tts::core {

std::string_view to_string(VoiceGender gender) noexcept {
    switch (gender) {
        case VoiceGender::female:  return "Female";
        case VoiceGender::male:    return "Male";
        case VoiceGender::unknown: return "Unknown";
    }
    return "Unknown";
}

common::Result<VoiceGender> voice_gender_from_string(std::string_view value) {
    if (value == "Female") return common::Result<VoiceGender>::ok(VoiceGender::female);
    if (value == "Male")   return common::Result<VoiceGender>::ok(VoiceGender::male);
    return common::Result<VoiceGender>::fail(
        common::Error{common::ErrorCode::invalid_argument,
                      "voice_gender_from_string: unrecognised value",
                      std::string{value}});
}

bool Voice::operator==(const Voice& o) const noexcept {
    return name               == o.name
        && short_name         == o.short_name
        && gender             == o.gender
        && locale             == o.locale
        && friendly_name      == o.friendly_name
        && status             == o.status
        && suggested_codec    == o.suggested_codec
        && content_categories == o.content_categories
        && voice_personalities== o.voice_personalities
        && language           == o.language;
}

bool Voice::operator!=(const Voice& o) const noexcept { return !(*this == o); }

} // namespace edge_tts::core
