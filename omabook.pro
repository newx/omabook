# omabook — a book library and reader for Omarchy.
#
# One .pro, one make, one binary, as omawrite does it. The two .pri files are
# file lists and nothing more; they exist so this stays a page long. Do not turn
# them into subdirs projects — the test build compiles src/core directly, which
# is what enforces the core/app boundary (CLAUDE.md, "Layout").

TEMPLATE = app
TARGET = omabook

QT += core gui qml quick quickcontrols2 quickdialogs2 svg
QT += sql network concurrent
QT += webenginequick webchannel
# QZipReader lives in QtCore's private headers; it is how EPUBs are read.
# This pins the binary to the Qt patch version it was built against (SPEC 7.6).
QT += core-private

CONFIG += c++17

# Baked in so `omabook --version` can say which build you are actually running.
# That is not idle curiosity: this application has existed as a Rust binary and
# a C++ one under the same name, installed side by side, and telling them apart
# by looking at a window is impossible. Empty when built from a tarball with no
# git, which is fine.
OMABOOK_VERSION = 0.1.0
OMABOOK_REVISION = $$system(git -C $$PWD describe --always --dirty --tags 2>/dev/null)
OMABOOK_BRANCH = $$system(git -C $$PWD rev-parse --abbrev-ref HEAD 2>/dev/null)
isEmpty(OMABOOK_REVISION): OMABOOK_REVISION = unknown
isEmpty(OMABOOK_BRANCH): OMABOOK_BRANCH = unknown
DEFINES += OMABOOK_VERSION=\\\"$$OMABOOK_VERSION\\\"
DEFINES += OMABOOK_REVISION=\\\"$$OMABOOK_REVISION\\\"
DEFINES += OMABOOK_BRANCH=\\\"$$OMABOOK_BRANCH\\\"

# Brute-force cosine over embedding vectors is the only hot loop in the app.
# -O3 lets GCC vectorize it; no -march, because this ships as an Arch package
# built for the plain x86-64 baseline.
QMAKE_CXXFLAGS_RELEASE += -O3

INCLUDEPATH += $$PWD/src

include(src/core/core.pri)
include(src/app/app.pri)

RESOURCES += \
    src/core/db/migrations.qrc \
    src/app/app.qrc
