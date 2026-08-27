# AI subsystem: policy, power, prompts, vector search, and the two provider
# implementations. indexer.* and assistant.* are a later round -- see
# TODO.md.

HEADERS += \
    $$PWD/anthropic.h \
    $$PWD/ollama.h \
    $$PWD/policy.h \
    $$PWD/power.h \
    $$PWD/prompts.h \
    $$PWD/provider.h \
    $$PWD/vectors.h

SOURCES += \
    $$PWD/anthropic.cpp \
    $$PWD/ollama.cpp \
    $$PWD/policy.cpp \
    $$PWD/power.cpp \
    $$PWD/prompts.cpp \
    $$PWD/provider.cpp \
    $$PWD/vectors.cpp
