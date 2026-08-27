# The AI bridge: summaries, questions and indexing, plus the worker thread
# that runs indexing off the GUI thread.

HEADERS += \
    $$PWD/aicontroller.h \
    $$PWD/indexworker.h

SOURCES += \
    $$PWD/aicontroller.cpp \
    $$PWD/indexworker.cpp
