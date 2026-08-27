// One error type and one result type for the core, standing in for Rust's
// `Result<T, E>`. Qt's event loop is not exception-safe (CLAUDE.md, "Error
// handling"), so nothing here throws; every fallible core function returns
// one of these instead and callers propagate with an early return.
#pragma once

#include <QString>
#include <optional>
#include <utility>

// A single error type with a variant per failure source, mirroring the
// Rust crate's `Error` enum (omabook-core/src/error.rs). `message` is
// user-facing and written as a sentence, so it can be shown as-is.
struct Error {
    enum class Kind { Io, Zip, Xml, Db, Net, Convert, Cancelled };

    Kind kind = Kind::Io;
    QString message;

    static Error io(const QString &message) { return Error{Kind::Io, message}; }
    static Error zip(const QString &message) { return Error{Kind::Zip, message}; }
    static Error xml(const QString &message) { return Error{Kind::Xml, message}; }
    static Error db(const QString &message) { return Error{Kind::Db, message}; }
    static Error net(const QString &message) { return Error{Kind::Net, message}; }
    static Error convert(const QString &message) { return Error{Kind::Convert, message}; }
    static Error cancelled(const QString &message) { return Error{Kind::Cancelled, message}; }
};

// Result<T>: either a value or an Error. Access value()/error() only after
// checking isOk() -- like the Rust type, reading the wrong side is a bug in
// the caller, not something this type recovers from at runtime.
template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }
    static Result err(Error error) { return Result(std::move(error)); }

    // Implicit on purpose: it is what makes RETURN_IF_ERR's `return
    // result.error();` work regardless of the enclosing function's T.
    Result(Error error) : m_error(std::move(error)) { }

    bool isOk() const { return m_value.has_value(); }
    bool isErr() const { return !isOk(); }

    const T &value() const { return *m_value; }
    T &value() { return *m_value; }

    const Error &error() const { return *m_error; }

private:
    explicit Result(T value) : m_value(std::move(value)) { }

    std::optional<T> m_value;
    std::optional<Error> m_error;
};

// Result<void>: for functions that are fallible but return nothing on
// success, matching Rust's `Result<()>`.
template <>
class Result<void> {
public:
    static Result ok() { return Result(); }
    static Result err(Error error) { return Result(std::move(error)); }

    bool isOk() const { return !m_error.has_value(); }
    bool isErr() const { return !isOk(); }

    const Error &error() const { return *m_error; }

private:
    Result() = default;
    explicit Result(Error error) : m_error(std::move(error)) { }

    std::optional<Error> m_error;
};

using VoidResult = Result<void>;

// Propagates a Result<E>-returning expression out of the current function.
// `expr` must be an lvalue-able Result; the enclosing function must itself
// return a Result whose error type matches. Named after Rust's `?` operator.
#define RETURN_IF_ERR(expr)                                                                     \
    do {                                                                                        \
        auto _result = (expr);                                                                  \
        if (_result.isErr())                                                                    \
            return decltype(_result)::err(_result.error());                                     \
    } while (0)
