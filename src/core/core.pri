# File lists for src/core, one per subsystem. Not sub-projects -- the test
# build compiles these sources directly, which is what enforces the core/app
# boundary (CLAUDE.md, "Layout").
#
# Split per directory only so that two people (or two agents) porting
# different subsystems do not queue behind one file.

HEADERS += \
    $$PWD/result.h \
    $$PWD/omarchy.h

SOURCES += \
    $$PWD/omarchy.cpp

include($$PWD/db/db.pri)
include($$PWD/models/models.pri)
include($$PWD/repo/repo.pri)
include($$PWD/import/import.pri)
include($$PWD/import/parsers.pri)
