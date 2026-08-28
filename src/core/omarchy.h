// Reading the active Omarchy theme.
//
// Omarchy writes the selected theme to `~/.local/state/omarchy/current/theme/`,
// whose `colors.toml` carries the palette every themed app on the system uses.
// Following it is what makes omabook look like it belongs (SPEC 3.5).
//
// This lives in core rather than app -- unlike the Rust build, where it sat
// beside main() -- because it is pure QtCore and because the watcher's
// behaviour across a real theme swap is the one thing here worth a test.
#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

// The whole palette the UI derives its tokens from (README "The mapping" /
// Theme.qml). Defaults are Omarchy's own "solitude" dark theme, so a machine
// with no Omarchy state directory still looks intentional rather than blank.
struct OmarchyTheme {
    QString name = QStringLiteral("unknown");
    bool dark = true;                                   // is the theme itself dark
    QString accent = QStringLiteral("#798186");
    QString background = QStringLiteral("#101315");
    QString darkBackground = QStringLiteral("#0c0e10");
    QString darkerBackground = QStringLiteral("#080a0b");
    QString lighterBackground = QStringLiteral("#101315");
    QString foreground = QStringLiteral("#cacccc");
    QString darkForeground = QStringLiteral("#4b4e55");
    QString brightForeground = QStringLiteral("#a5aeb4");
    QString selection = QStringLiteral("#343d41");
    QString muted = QStringLiteral("#4b4e55");

    bool operator==(const OmarchyTheme &other) const;
    bool operator!=(const OmarchyTheme &other) const { return !(*this == other); }
};

namespace Omarchy {

QString stateDir();

// Read the active theme, falling back to sane defaults when Omarchy is absent
// -- omabook must still run on a plain desktop.
OmarchyTheme current();

// The body of current(), with the directory named so a test can drive it.
OmarchyTheme readFrom(const QString &stateDir);

// `colors.toml` is a flat file of `key = "value"` lines. Rather than take a
// TOML dependency for that, parse exactly this shape -- and ignore anything
// that is not it, so a richer file cannot break us.
QHash<QString, QString> parseSimpleToml(const QString &text);

// How long to wait for the directory to stop changing before reading it.
//
// One theme switch is not one event. `omarchy-theme-set` removes the theme
// directory, moves the new one into its place, then writes `theme.name`, and
// re-pointing the background symlink lands afterwards. Reading on the first
// event catches the gap where `colors.toml` does not exist yet and flashes the
// fallback palette across the whole window.
constexpr int SETTLE_MS = 150;

} // namespace Omarchy

// Emits themeChanged() whenever the active Omarchy theme changes.
//
// Watches the *parent* of the theme directory, which is the only thing that
// works: `current/theme` is a real directory that gets deleted and replaced
// wholesale, so a watch on it follows the old inode into the bin and never
// fires again. `current/` itself survives the swap and sees it happen.
class OmarchyWatcher : public QObject {
    Q_OBJECT

public:
    explicit OmarchyWatcher(QObject *parent = nullptr);

    // Returns false on a desktop with no Omarchy state directory -- the
    // palette simply stays at whatever current() returned.
    bool watchDefault();
    bool watch(const QString &directory);

signals:
    void themeChanged();

private:
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
};
