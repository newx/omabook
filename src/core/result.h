// One error type and one result type for the core. Qt's event loop is not
// exception-safe (CLAUDE.md, "Error
// handling"), so nothing here throws; every fallible core function returns
// one of these instead and callers propagate with an early return.
#pragma once

#include <QString>
#include <optional>
#include <utility>

// A single error type with a variant per failure source. `message` is
// user-facing and written as a sentence, so it can be shown as-is.
struct Error {
    // Decode is a stored value that is no longer meaningful -- an unknown
    // status string, a vector blob of the wrong length. Convert is an
    // external converter (ebook-convert) that failed. They read alike and
    // are not: the first means the database disagrees with this build.
    enum class Kind { Io, Zip, Xml, Db, Net, Decode, Convert, Cancelled };

    Kind kind = Kind::Io;
    QString message;

    static Error io(const QString &message) { return Error{Kind::Io, message}; }
    static Error zip(const QString &message) { return Error{Kind::Zip, message}; }
    static Error xml(const QString &message) { return Error{Kind::Xml, message}; }
    static Error db(const QString &message) { return Error{Kind::Db, message}; }
    static Error net(const QString &message) { return Error{Kind::Net, message}; }
    static Error decode(const QString &message) { return Error{Kind::Decode, message}; }
    static Error convert(const QString &message) { return Error{Kind::Convert, message}; }
    static Error cancelled(const QString &message) { return Error{Kind::Cancelled, message}; }
};

// Result<T>: either a value or an Error. Access value()/error() only after
// checking isOk(): reading the wrong side is a bug in the caller, not
// something this type recovers from at runtime.
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
// success.
template <>
class Result<void> {
public:
    static Result ok() { return Result(); }
    static Result err(Error error) { return Result(std::move(error)); }

    // Implicit, to match Result<T>: it is what lets a bare `return
    // result.error();` inside a Result<void>-returning function work.
    Result(Error error) : m_error(std::move(error)) { }

    bool isOk() const { return !m_error.has_value(); }
    bool isErr() const { return !isOk(); }

    const Error &error() const { return *m_error; }

private:
    Result() = default;

    std::optional<Error> m_error;
};

using VoidResult = Result<void>;

// Propagates a Result<E>-returning expression out of the current function.
// Relies on Result<T>'s implicit Error constructor, so the enclosing
// function may return any Result<T> -- not just the same T as `expr` -- so
// one error type threads through functions with different Ok types.
#define RETURN_IF_ERR(expr)                                                                     \
    do {                                                                                        \
        auto _result = (expr);                                                                  \
        if (_result.isErr())                                                                    \
            return _result.error();                                                             \
    } while (0)
