# AI subsystem: policy, power, prompts, vector search, the two provider
# implementations, chunking/embedding, and the assistant.

HEADERS += \
    $$PWD/anthropic.h \
    $$PWD/ollama.h \
    $$PWD/policy.h \
    $$PWD/power.h \
    $$PWD/prompts.h \
    $$PWD/provider.h \
    $$PWD/vectors.h \
    $$PWD/indexer.h \
    $$PWD/assistant.h

SOURCES += \
    $$PWD/anthropic.cpp \
    $$PWD/ollama.cpp \
    $$PWD/policy.cpp \
    $$PWD/power.cpp \
    $$PWD/prompts.cpp \
    $$PWD/provider.cpp \
    $$PWD/vectors.cpp \
    $$PWD/indexer.cpp \
    $$PWD/assistant.cpp
