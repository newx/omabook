#include "omarchy.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

bool OmarchyTheme::operator==(const OmarchyTheme &other) const {
    return name == other.name && dark == other.dark && accent == other.accent
        && background == other.background && darkBackground == other.darkBackground
        && darkerBackground == other.darkerBackground
        && lighterBackground == other.lighterBackground && foreground == other.foreground
        && darkForeground == other.darkForeground && brightForeground == other.brightForeground
        && selection == other.selection && muted == other.muted;
}

namespace {

// Standard perceived-luminance weighting. Used only when the theme file omits
// `mode`, so a background colour still tells us whether the theme is dark.
bool isDarkColour(const QString &hex) {
    const QColor colour(hex);
    if (!colour.isValid())
        return true; // unparsable background: keep the safer dark guess

    const double luminance =
        0.299 * colour.redF() + 0.587 * colour.greenF() + 0.114 * colour.blueF();
    return luminance < 0.5;
}

} // namespace

namespace Omarchy {

QString stateDir() {
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
}

QHash<QString, QString> parseSimpleToml(const QString &text) {
    QHash<QString, QString> values;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
                || line.startsWith(QLatin1Char('[')))
            continue;

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;

        QString value = line.mid(equals + 1).trimmed();
        // Quotes are stripped from either end rather than matched as a pair,
        // because that is what the Rust trim_matches did and a half-quoted
        // value in a hand-edited theme should still read as its contents.
        while (value.size() >= 1
                && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\''))))
            value = value.mid(1);
        while (value.size() >= 1
                && (value.endsWith(QLatin1Char('"')) || value.endsWith(QLatin1Char('\''))))
            value.chop(1);

        if (value.isEmpty())
            continue;

        values.insert(line.left(equals).trimmed(), value);
    }

    return values;
}

OmarchyTheme readFrom(const QString &stateDirectory) {
    const QDir dir(stateDirectory);
    OmarchyTheme theme;

    QFile nameFile(dir.filePath(QStringLiteral("theme.name")));
    if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
        if (!name.isEmpty())
            theme.name = name;
    }

    QFile colorsFile(dir.filePath(QStringLiteral("theme/colors.toml")));
    if (!colorsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qInfo("no Omarchy theme found; using built-in defaults");
        return theme;
    }

    const QHash<QString, QString> values =
        parseSimpleToml(QString::fromUtf8(colorsFile.readAll()));

    theme.accent = values.value(QStringLiteral("accent"), theme.accent);
    theme.background = values.value(QStringLiteral("background"), theme.background);
    theme.darkBackground = values.value(QStringLiteral("dark_background"), theme.darkBackground);
    theme.darkerBackground =
        values.value(QStringLiteral("darker_background"), theme.darkerBackground);
    theme.lighterBackground =
        values.value(QStringLiteral("lighter_background"), theme.lighterBackground);
    theme.foreground = values.value(QStringLiteral("foreground"), theme.foreground);
    theme.darkForeground = values.value(QStringLiteral("dark_foreground"), theme.darkForeground);
    theme.brightForeground =
        values.value(QStringLiteral("bright_foreground"), theme.brightForeground);
    theme.selection = values.value(QStringLiteral("selection"), theme.selection);
    theme.muted = values.value(QStringLiteral("muted"), theme.muted);

    // A missing `mode` is inferred from the background's luminance rather than
    // assumed dark, so a hand-edited or unusual theme still classifies itself
    // correctly instead of just defaulting.
    if (values.contains(QStringLiteral("mode")))
        theme.dark = values.value(QStringLiteral("mode")) != QStringLiteral("light");
    else
        theme.dark = isDarkColour(theme.background);

    return theme;
}

OmarchyTheme current() {
    return readFrom(stateDir());
}

} // namespace Omarchy

OmarchyWatcher::OmarchyWatcher(QObject *parent) : QObject(parent) {
    m_settle.setSingleShot(true);
    m_settle.setInterval(Omarchy::SETTLE_MS);
    connect(&m_settle, &QTimer::timeout, this, &OmarchyWatcher::themeChanged);

    // Every event restarts the timer, so a burst collapses into one refresh
    // once the directory has been quiet for the settle window.
    const auto poke = [this]() { m_settle.start(); };
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, poke);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, poke);
}

bool OmarchyWatcher::watchDefault() {
    return watch(Omarchy::stateDir());
}

bool OmarchyWatcher::watch(const QString &directory) {
    if (!QFileInfo::exists(directory)) {
        qInfo("not watching for theme changes: %s does not exist",
              qUtf8Printable(directory));
        return false;
    }

    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);

    return m_watcher.addPath(directory);
}
