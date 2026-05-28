#include "edge_tts/communication/VoicesManager.hpp"

#include <algorithm>
#include <utility>

namespace edge_tts::communication {

VoicesManager::VoicesManager(HttpVoiceService service) : service_(std::move(service)) {}

std::vector<core::Voice> VoicesManager::list() const {
    return service_.list_voices();
}

std::vector<core::Voice> VoicesManager::find_by_locale(const std::string& locale) const {
    auto voices = list();
    voices.erase(std::remove_if(voices.begin(), voices.end(), [&](const core::Voice& voice) {
        return voice.locale != locale;
    }), voices.end());
    return voices;
}

} // namespace edge_tts::communication
