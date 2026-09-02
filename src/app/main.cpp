#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QQmlContext>
#include <QtWebEngineQuick>

#include <cstdio>
#include <cstring>

#include "bridge/librarymodel.h"
#include "bridge/notesmodel.h"
#include "bridge/readerbridge.h"
#include "bridge/sidebarmodel.h"
#include "bridge/thememodel.h"

// The QML module the .qml files import.
static const char *const QML_URI = "com.omabook.app";

// Printed with printf rather than qInfo on purpose. Arch builds qt6-base with
// journald support, so Qt routes every log category to the journal whenever
// stderr is not a terminal -- a --version that answered into the system
// journal would be a joke (CLAUDE.md, "Traps").
static void printVersion() {
    printf("omabook %s\n", OMABOOK_VERSION);
    printf("  build   %s %s (C++ / Qt Quick)\n", OMABOOK_BRANCH, OMABOOK_REVISION);
    // Both Qt versions, because QZipReader comes from Qt's private headers: a
    // binary run against a Qt it was not built against may crash at any
    // arbitrary point (SPEC 7.6). If these two disagree, rebuild before
    // investigating anything else.
    printf("  Qt      built against %s, running on %s\n", QT_VERSION_STR, qVersion());
}

int main(int argc, char *argv[]) {
    // Answered before anything else, so it needs no display, no web engine and
    // no database -- and so it still answers on a machine where the app itself
    // cannot start.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printVersion();
            return 0;
        }
    }

    // QtWebEngineQuick::initialize() must run before the application object is
    // constructed. An application controls its own main(), which is exactly the
    // requirement a Quickshell plugin could never satisfy (SPEC 2.2), and it is
    // the reason omabook can host a web engine at all.
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omabook"));
    app.setDesktopFileName(QStringLiteral("omabook"));
    app.setOrganizationName(QStringLiteral("omabook"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omabook")));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // The QML declares its own backend instances (`ThemeModel { id: themeModel }`).
    // That is what lets each of these exist more than once with independent
    // state (SPEC 5.13), which a context property could not do.
    qmlRegisterType<ThemeModel>(QML_URI, 1, 0, "ThemeModel");
    qmlRegisterType<LibraryModel>(QML_URI, 1, 0, "LibraryModel");
    qmlRegisterType<SidebarModel>(QML_URI, 1, 0, "SidebarModel");
    qmlRegisterType<NotesModel>(QML_URI, 1, 0, "NotesModel");
    qmlRegisterType<ReaderBridge>(QML_URI, 1, 0, "ReaderBridge");

    // A pragma Singleton is not resolved by the implicit directory import the
    // way an ordinary sibling component is; without this it reads as
    // "Theme is not defined" the first time a screen is built.
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Theme.qml")), QML_URI, 1, 0, "Theme");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Icons.qml")), QML_URI, 1, 0, "Icons");

    QQmlApplicationEngine engine;
    // Runtime QML warnings are otherwise invisible: a build that prints nothing
    // proves nothing, and a binding typo shows up as an empty pane.
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the OmaBooks interface.";
        return -1;
    }

    return app.exec();
}
