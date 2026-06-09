#pragma once

#include "common/Error.hpp"

#include <optional>
#include <stdexcept>
#include <variant>

namespace edge_tts::common {

// Thrown when value() is called on a failure Result, or error() is called on
// a success Result.  These are programming errors — catch to test, not to recover.
class BadResultAccess : public std::logic_error {
    using std::logic_error::logic_error;
};

// ---------------------------------------------------------------------------
// Result<T> — holds either a T value (success) or an Error (failure).
//
// Construction
//   Result<int> r = Result<int>::ok(42);
//   Result<int> r = Result<int>::fail(Error{ErrorCode::io_error, "read failed"});
//
// Inspection
//   if (r) { use(*r); }           // operator bool / operator*
//   if (r.has_value()) { use(r.value()); }
//   else { log(r.error().what()); }
// ---------------------------------------------------------------------------
template<typename T>
class Result {
    std::variant<T, Error> data_;

    explicit Result(std::in_place_index_t<0>, T v)
        : data_(std::in_place_index<0>, std::move(v)) {}
    explicit Result(std::in_place_index_t<1>, Error e)
        : data_(std::in_place_index<1>, std::move(e)) {}

public:
    [[nodiscard]] static Result ok(T value) {
        return Result{std::in_place_index<0>, std::move(value)};
    }
    [[nodiscard]] static Result fail(Error e) {
        return Result{std::in_place_index<1>, std::move(e)};
    }

    // Move-only support: Result itself is moveable/copyable depending on T.
    Result(Result&&)            = default;
    Result& operator=(Result&&) = default;
    Result(const Result&)       = default;
    Result& operator=(const Result&) = default;

    [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }
    explicit operator bool()       const noexcept { return has_value(); }

    // value() — throws BadResultAccess if this is a failure Result.
    [[nodiscard]] T& value() {
        if (!has_value())
            throw BadResultAccess{"Result::value() called on a failure Result: "
                                  + std::get<Error>(data_).what()};
        return std::get<T>(data_);
    }
    [[nodiscard]] const T& value() const {
        if (!has_value())
            throw BadResultAccess{"Result::value() called on a failure Result: "
                                  + std::get<Error>(data_).what()};
        return std::get<T>(data_);
    }

    // operator* / operator-> — undefined behaviour if !has_value(); use when
    // has_value() is already known.
    [[nodiscard]] T&       operator*()       noexcept { return std::get<T>(data_); }
    [[nodiscard]] const T& operator*() const noexcept { return std::get<T>(data_); }
    [[nodiscard]] T*       operator->()       noexcept { return &std::get<T>(data_); }
    [[nodiscard]] const T* operator->() const noexcept { return &std::get<T>(data_); }

    // error() — throws BadResultAccess if this is a success Result.
    [[nodiscard]] Error& error() {
        if (has_value())
            throw BadResultAccess{"Result::error() called on a success Result"};
        return std::get<Error>(data_);
    }
    [[nodiscard]] const Error& error() const {
        if (has_value())
            throw BadResultAccess{"Result::error() called on a success Result"};
        return std::get<Error>(data_);
    }
};

// ---------------------------------------------------------------------------
// Result<void> — holds either success (no value) or an Error.
//
// Construction
//   Result<void> r = Result<void>::ok();
//   Result<void> r = Result<void>::fail(Error{ErrorCode::io_error, "write failed"});
// ---------------------------------------------------------------------------
template<>
class Result<void> {
    std::optional<Error> error_;

    explicit Result(std::optional<Error> e) : error_(std::move(e)) {}

public:
    [[nodiscard]] static Result ok()       { return Result{std::nullopt}; }
    [[nodiscard]] static Result fail(Error e) {
        return Result{std::optional<Error>{std::move(e)}};
    }

    Result(Result&&)            = default;
    Result& operator=(Result&&) = default;
    Result(const Result&)       = default;
    Result& operator=(const Result&) = default;

    [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }
    explicit operator bool()       const noexcept { return has_value(); }

    // value() — throws BadResultAccess if this is a failure Result.
    void value() const {
        if (!has_value())
            throw BadResultAccess{"Result<void>::value() called on a failure Result: "
                                  + error_->what()};
    }

    [[nodiscard]] Error& error() {
        if (has_value())
            throw BadResultAccess{"Result<void>::error() called on a success Result"};
        return *error_;
    }
    [[nodiscard]] const Error& error() const {
        if (has_value())
            throw BadResultAccess{"Result<void>::error() called on a success Result"};
        return *error_;
    }
};

} // namespace edge_tts::common
