#pragma once

#include <stdexcept>
#include <variant>

namespace edge_tts::common {

// Sentinel wrapper used to construct an Expected in the error state.
template<typename E>
struct Unexpected {
    E error;
    explicit Unexpected(E e) noexcept(std::is_nothrow_move_constructible_v<E>)
        : error(std::move(e)) {}
};

template<typename E>
Unexpected(E) -> Unexpected<E>;

// Lightweight Expected<T,E> for C++20.  Semantics mirror std::expected (C++23).
// Only the subset used by this project is implemented.
template<typename T, typename E>
class Expected {
    std::variant<T, E> data_;

public:
    // Construct in the value state.
    // NOLINTNEXTLINE(google-explicit-constructor)
    Expected(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : data_(std::in_place_index<0>, std::move(value)) {}

    // Construct in the error state.
    // NOLINTNEXTLINE(google-explicit-constructor)
    Expected(Unexpected<E> unexp) noexcept(std::is_nothrow_move_constructible_v<E>)
        : data_(std::in_place_index<1>, std::move(unexp.error)) {}

    [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() {
        if (!has_value()) throw std::runtime_error{"Expected: no value"};
        return std::get<0>(data_);
    }
    [[nodiscard]] const T& value() const {
        if (!has_value()) throw std::runtime_error{"Expected: no value"};
        return std::get<0>(data_);
    }
    [[nodiscard]] T& operator*() noexcept { return std::get<0>(data_); }
    [[nodiscard]] const T& operator*() const noexcept { return std::get<0>(data_); }
    [[nodiscard]] T* operator->() noexcept { return &std::get<0>(data_); }
    [[nodiscard]] const T* operator->() const noexcept { return &std::get<0>(data_); }

    [[nodiscard]] E& error() noexcept { return std::get<1>(data_); }
    [[nodiscard]] const E& error() const noexcept { return std::get<1>(data_); }
};

} // namespace edge_tts::common
