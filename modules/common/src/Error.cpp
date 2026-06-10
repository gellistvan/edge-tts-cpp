#include "common/Error.hpp"

namespace edge_tts::common {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::none:                    return "none";
        case ErrorCode::invalid_argument:        return "invalid_argument";
        case ErrorCode::invalid_state:           return "invalid_state";
        case ErrorCode::io_error:                return "io_error";
        case ErrorCode::network_error:           return "network_error";
        case ErrorCode::protocol_error:          return "protocol_error";
        case ErrorCode::parse_error:             return "parse_error";
        case ErrorCode::timeout:                 return "timeout";
        case ErrorCode::unsupported:             return "unsupported";
        case ErrorCode::external_process_failed: return "external_process_failed";
        case ErrorCode::service_error:           return "service_error";
        case ErrorCode::drm_error:               return "drm_error";
        case ErrorCode::cancelled:               return "cancelled";
    }
    return "unknown";
}

Error::Error(ErrorCode code, std::string message)
    : code_(code), message_(std::move(message)) {}

Error::Error(ErrorCode code, std::string message, std::string context)
    : code_(code), message_(std::move(message)), context_(std::move(context)) {}

ErrorCode Error::code() const noexcept { return code_; }

std::string_view Error::message() const noexcept { return message_; }

std::string_view Error::context() const noexcept { return context_; }

bool Error::has_context() const noexcept { return !context_.empty(); }

std::string Error::what() const {
    std::string s;
    s += '[';
    s += to_string(code_);
    s += "] ";
    s += message_;
    if (!context_.empty()) {
        s += " (context: ";
        s += context_;
        s += ')';
    }
    return s;
}

} // namespace edge_tts::common
