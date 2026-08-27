#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QQmlContext>
#include <QtWebEngineQuick>

#include "bridge/aicontroller.h"
#include "bridge/librarymodel.h"
#include "bridge/notesmodel.h"
#include "bridge/readerbridge.h"
#include "bridge/sidebarmodel.h"
#include "bridge/thememodel.h"
#include "bridge/ttscontroller.h"

// The QML module the .qml files import. Kept from the Rust build so the
// existing `import com.omabook.app` lines still resolve.
static const char *const QML_URI = "com.omabook.app";

int main(int argc, char *argv[]) {
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

    // The QML declares its own backend instances (`ThemeModel { id: themeModel }`),
    // as the Rust build's module did. That is what lets AiController and
    // TtsController exist more than once with independent state (SPEC 5.13),
    // which a context property could not do.
    qmlRegisterType<ThemeModel>(QML_URI, 1, 0, "ThemeModel");
    qmlRegisterType<LibraryModel>(QML_URI, 1, 0, "LibraryModel");
    qmlRegisterType<AiController>(QML_URI, 1, 0, "AiController");
    qmlRegisterType<SidebarModel>(QML_URI, 1, 0, "SidebarModel");
    qmlRegisterType<NotesModel>(QML_URI, 1, 0, "NotesModel");
    qmlRegisterType<ReaderBridge>(QML_URI, 1, 0, "ReaderBridge");
    qmlRegisterType<TtsController>(QML_URI, 1, 0, "TtsController");

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
