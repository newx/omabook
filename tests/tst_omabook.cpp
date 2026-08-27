// One test binary, one class per subsystem.
//
// omawrite has a single test class in a single file, which is right for six
// source files. At this size that file would be the one everybody edits, so
// each subsystem gets its own header and this main() runs them in turn.
// QTEST_MAIN would only take one class, hence the hand-written main.

#include <QCoreApplication>
#include <QStandardPaths>
#include <QtTest>

#include "test_core.h"
#include "test_repo.h"
#include "test_import.h"
#include "test_parsers.h"
#include "test_ai.h"
#include "test_tts.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    // Nothing may write to the real ~/.local/share/omabook.
    QStandardPaths::setTestModeEnabled(true);

    int status = 0;
    const auto run = [&](QObject *test) { status |= QTest::qExec(test, argc, argv); };
    { CoreTest t;    run(&t); }
    { RepoTest t;    run(&t); }
    { ImportTest t;  run(&t); }
    { ParsersTest t; run(&t); }
    { AiTest t;      run(&t); }
    { TtsTest t;     run(&t); }
    return status;
}
