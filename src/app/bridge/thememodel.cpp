#include "thememodel.h"

#include "core/db/database.h"
#include "core/repo/settingsrepository.h"

namespace {
const QString SETTING_KEY = QStringLiteral("theme_mode");
}

bool ThemeModel::resolve(const QString &mode, bool systemDark) {
    if (mode == QStringLiteral("dark"))
        return true;
    if (mode == QStringLiteral("light"))
        return false;
    return systemDark;
}

ThemeModel::ThemeModel(QObject *parent) : QObject(parent) {
    m_palette = Omarchy::current();

    SettingsRepository settings(Database::forCurrentThread().connection());
    m_mode = settings.getOr(SETTING_KEY, QStringLiteral("system"));
    m_dark = resolve(m_mode, m_palette.dark);
}

void ThemeModel::cycleMode() {
    if (m_mode == QStringLiteral("system"))
        applyMode(QStringLiteral("dark"));
    else if (m_mode == QStringLiteral("dark"))
        applyMode(QStringLiteral("light"));
    else
        applyMode(QStringLiteral("system"));
}

void ThemeModel::applyMode(const QString &mode) {
    SettingsRepository settings(Database::forCurrentThread().connection());
    const Result<void> stored = settings.set(SETTING_KEY, mode);
    if (stored.isErr())
        qWarning("could not persist theme mode: %s", qUtf8Printable(stored.error().message));

    const bool dark = resolve(mode, m_palette.dark);

    if (m_mode != mode) {
        m_mode = mode;
        emit modeChanged();
    }
    if (m_dark != dark) {
        m_dark = dark;
        emit darkChanged();
    }
}

void ThemeModel::followSystemTheme() {
    if (m_watcher)
        return;

    m_watcher = new OmarchyWatcher(this);
    connect(m_watcher, &OmarchyWatcher::themeChanged, this, &ThemeModel::refreshSystemTheme);
    if (!m_watcher->watchDefault()) {
        // No Omarchy state directory: the palette simply stays where it is.
        delete m_watcher;
        m_watcher = nullptr;
    }
}

void ThemeModel::refreshSystemTheme() {
    const OmarchyTheme theme = Omarchy::current();
    const bool dark = resolve(m_mode, theme.dark);

    // Compare the whole palette before touching anything. Every property
    // write emits its changed signal, and Theme is bound into most of the UI,
    // so an unconditional refresh would relayout the window each time the
    // directory is touched -- including for the background symlink, which is
    // not a palette change at all.
    if (m_palette != theme) {
        m_palette = theme;
        emit paletteChanged();
    }
    if (m_dark != dark) {
        m_dark = dark;
        emit darkChanged();
    }
}
