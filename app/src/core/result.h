// Result<T>: the value of a fallible call, or an error message. Used instead of exceptions so that
// failures are explicit at call sites (CLAUDE.md: no exceptions on real-time paths).
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace psyight {

struct Error {
    std::string message;
};

template <typename T> class Result {
  public:
    Result(T value) : state_(std::move(value)) {}
    Result(Error error) : state_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(state_); }
    explicit operator bool() const { return ok(); }

    // Precondition: ok().
    T &value() { return std::get<T>(state_); }
    const T &value() const { return std::get<T>(state_); }

    // Precondition: !ok().
    const Error &error() const { return std::get<Error>(state_); }

  private:
    std::variant<T, Error> state_;
};

// Result for calls that return nothing on success.
template <> class Result<void> {
  public:
    Result() = default;
    Result(Error error) : error_(std::move(error)), ok_(false) {}

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }

    // Precondition: !ok().
    const Error &error() const { return error_; }

  private:
    Error error_;
    bool ok_ = true;
};

} // namespace psyight
