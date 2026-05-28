#pragma once

#include <stdexcept>

namespace edge_tts::common {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ConfigurationError final : public Error { using Error::Error; };
class NetworkError final : public Error { using Error::Error; };
class ProtocolError final : public Error { using Error::Error; };
class AudioError final : public Error { using Error::Error; };
class SubtitleError final : public Error { using Error::Error; };

} // namespace edge_tts::common
