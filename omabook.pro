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
QT += webenginequick webchannel multimedia texttospeech
# QZipReader lives in QtCore's private headers; it is how EPUBs are read.
# This pins the binary to the Qt patch version it was built against (SPEC 7.6).
QT += core-private

CONFIG += c++17

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
