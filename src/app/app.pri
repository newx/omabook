# The front end: QObjects exposed to QML, and main().
#
# Every .qml file must also be listed in app.qrc. An unregistered one is not a
# build error -- the type silently does not exist and the screen renders empty.

HEADERS += \
    $$PWD/assets.h \
    $$PWD/bridge/thememodel.h

SOURCES += \
    $$PWD/assets.cpp \
    $$PWD/bridge/thememodel.cpp \
    $$PWD/main.cpp

# One file list per bridge, for the same reason core is split per subsystem.
include($$PWD/bridge/library.pri)
include($$PWD/bridge/notes.pri)
include($$PWD/bridge/reader.pri)
