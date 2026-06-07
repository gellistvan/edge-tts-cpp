#pragma once

// Suppress deprecation warnings from HttpVoiceService used within this legacy wrapper.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "edge_tts/communication/HttpVoiceService.hpp"
#pragma GCC diagnostic pop

#include "edge_tts/core/Voice.hpp"

#include <string>
#include <vector>

namespace edge_tts::communication {

class [[deprecated("Use VoiceService with EdgeTokenProvider injection instead")]]
VoicesManager final {
public:
    explicit VoicesManager(HttpVoiceService service = {});

    [[nodiscard]] std::vector<core::Voice> list() const;
    [[nodiscard]] std::vector<core::Voice> find_by_locale(const std::string& locale) const;

private:
    HttpVoiceService service_;
};

} // namespace edge_tts::communication
