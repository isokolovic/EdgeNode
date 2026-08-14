#pragma once

#include <type_traits>
#include <utility>
#include <cassert>

namespace coretypes {

/// @brief A discriminated union holding either a success value T or an error value E.
/// Models the "either value or error" pattern without exceptions.
template <typename T, typename E>
class Result
{
public:
    /// @brief Construct a Result in the success state.
    //static = class-level function (factory method pattern)
    static Result ok(const T& value) 
    {
        Result r;
        r.val = value;
        r.has_val = true;
        return r;
    }

    /// @brief Construct a Result in the success state (move).
	/// Needed for types that are move-only or expensive to copy.
    static Result ok(T&& value)
    {
        Result r;
        r.val = std::move(value);
        r.has_val = true;
        return r;
    }

    /// @brief Construct a Result in the error state.
    static Result err(const E& error)
    {
        Result r;
        r.errc = error;
        r.has_val = false;
        return r;
    }

    /// @brief Construct a Result in the error state (move).
    static Result err(E&& error)
    {
        Result r;
        r.errc = std::move(error);
        r.has_val = false;
        return r;
    }

    /// @brief Return true when this Result holds a value.
    bool is_ok() const { return has_val; }

    /// @brief Return true when this Result holds an error.
    bool is_err() const { return !has_val; }

    /// @brief Access the value. Caller must check is_ok() first.
    const T& value() const { assert(has_val); return val; }

    /// @brief Access the value. Caller must check is_ok() first.
    T& value() { assert(has_val); return val; }

    /// @brief Access the error. Caller must check is_err() first.
    const E& error() const { assert(!has_val); return errc; }

    /// @brief Access the error. Caller must check is_err() first.
    E& error() { assert(!has_val); return errc; }

    /// @brief Return the value if ok, otherwise return the provided fallback.
    T value_or(const T& fallback) const { return has_val ? val : fallback; }

private:
    Result() = default;

    T val{};
    E errc{};
    bool has_val = false;
};

} // namespace coretypes
