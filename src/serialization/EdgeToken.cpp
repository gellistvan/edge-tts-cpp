#include "edge_tts/serialization/EdgeToken.hpp"

namespace edge_tts::serialization {

std::string EdgeToken::trusted_client_token() {
    return "TODO_TRUSTED_CLIENT_TOKEN";
}

std::string EdgeToken::generate_connection_id() {
    return "TODO_CONNECTION_ID";
}

std::string EdgeToken::generate_sec_ms_gec(clock::time_point) {
    return "TODO_SEC_MS_GEC";
}

} // namespace edge_tts::serialization
