#pragma once

#include <chrono>
#include <string>

namespace edge_tts::serialization {

class EdgeToken final {
public:
    using clock = std::chrono::system_clock;

    [[nodiscard]] static std::string trusted_client_token();
    [[nodiscard]] static std::string generate_connection_id();
    [[nodiscard]] static std::string generate_sec_ms_gec(clock::time_point now = clock::now());
};

} // namespace edge_tts::serialization
