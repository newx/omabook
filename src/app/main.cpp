#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QtWebEngineQuick>

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
