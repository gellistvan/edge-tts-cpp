#pragma once

#include "edge_tts/communication/HttpVoiceService.hpp"
#include "edge_tts/core/Voice.hpp"

#include <string>
#include <vector>

namespace edge_tts::communication {

class VoicesManager final {
public:
    explicit VoicesManager(HttpVoiceService service = {});

    [[nodiscard]] std::vector<core::Voice> list() const;
    [[nodiscard]] std::vector<core::Voice> find_by_locale(const std::string& locale) const;

private:
    HttpVoiceService service_;
};

} // namespace edge_tts::communication
