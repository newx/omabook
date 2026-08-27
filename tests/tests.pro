# The test build is also the core/app boundary guard.
#
# QT below lists no qml, quick, webenginequick or multimedia, so qmake never
# adds those include paths and an #include <QQmlEngine> under src/core is a
# compile error rather than a code review comment. bin/test runs this every
# time. gui stays because covers are thumbnailed with QImageReader, and
# core-private because EPUBs are read with QZipReader.

QT += sql network concurrent testlib core-private
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_omabook

INCLUDEPATH += $$PWD/../src

include(../src/core/core.pri)

SOURCES += tst_omabook.cpp
RESOURCES += ../src/core/db/migrations.qrc
