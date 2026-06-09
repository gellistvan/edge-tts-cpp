#pragma once

#include <stdexcept>

namespace edge_tts::common {

// Base exception for all edge-tts-cpp runtime failures raised as C++ exceptions.
// Prefer returning Result<T> for recoverable errors; throw these only at
// API boundary-validation points (e.g. TtsConfig::validate()).
class Exception : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ConfigurationError final : public Exception { using Exception::Exception; };
class NetworkError      final : public Exception { using Exception::Exception; };
class ProtocolError     final : public Exception { using Exception::Exception; };
class AudioError        final : public Exception { using Exception::Exception; };
class SubtitleError     final : public Exception { using Exception::Exception; };

} // namespace edge_tts::common
