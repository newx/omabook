# The front end: QObjects exposed to QML, and main().
#
# Every .qml file must also be listed in app.qrc. An unregistered one is not a
# build error -- the type silently does not exist and the screen renders empty.

HEADERS +=

SOURCES += \
    $$PWD/main.cpp
