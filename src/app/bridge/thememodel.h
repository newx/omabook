// ThemeModel -- the palette, and the light/dark switch.
//
// Follows the active Omarchy theme for its accent (SPEC 3.5) while keeping the
// surfaces deliberately plain: a near-black ground in dark mode, matching the
// terminal, and flat grey borders rather than shadows or gradients.
#pragma once

#include <QObject>
#include <QString>

#include "core/omarchy.h"

class ThemeModel : public QObject {
    Q_OBJECT
    // "system", "dark" or "light" -- what the user chose.
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    // What that resolves to right now.
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(QString accent READ accent NOTIFY accentChanged)
    // Snake case, and deliberately: the QML reads `themeModel.theme_name`,
    // because cxx-qt never camelCased a multi-word qproperty and the ported
    // QML carries those names byte-for-byte. The C++ accessors stay camelCase;
    // only the QML-visible token differs. Renaming it here breaks the binding
    // silently -- an undefined read, no warning (CLAUDE.md, "Traps").
    Q_PROPERTY(QString theme_name READ themeName NOTIFY themeNameChanged)

public:
    explicit ThemeModel(QObject *parent = nullptr);

    QString mode() const { return m_mode; }
    bool dark() const { return m_dark; }
    QString accent() const { return m_accent; }
    QString themeName() const { return m_themeName; }

    // Cycle system -> dark -> light -> system, persisting the choice.
    Q_INVOKABLE void cycleMode();

    // Not named setMode: the `mode` property's notifier already claims that
    // name, and the overload is ambiguous to moc.
    Q_INVOKABLE void applyMode(const QString &mode);

    // Start following the desktop: re-read the palette whenever Omarchy
    // switches theme. Called from QML rather than from the constructor,
    // because the object has to exist before anything may signal it.
    Q_INVOKABLE void followSystemTheme();

    // Re-read the active Omarchy theme and adopt anything that moved.
    Q_INVOKABLE void refreshSystemTheme();

    // "system" defers to the Omarchy theme; anything else is an explicit
    // choice, and anything unrecognised is treated as "system" rather than
    // failing. Static so it is testable without a window or a database.
    static bool resolve(const QString &mode, bool systemDark);

signals:
    void modeChanged();
    void darkChanged();
    void accentChanged();
    void themeNameChanged();

private:
    QString m_mode = QStringLiteral("system");
    bool m_dark = true;
    QString m_accent;
    QString m_themeName;
    bool m_systemDark = true;
    OmarchyWatcher *m_watcher = nullptr;
};
