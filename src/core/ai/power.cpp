#include "core/ai/power.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>

namespace {

const QString kSupplyDir = QStringLiteral("/sys/class/power_supply");

// Reads one field file (e.g. "type", "online") from a supply directory,
// trimmed. A missing file (not every supply has every field) reads as an
// empty string, matching the Rust build's unwrap_or_default().
QString field(const QString &supplyPath, const QString &name) {
    QFile file(QDir(supplyPath).filePath(name));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll()).trimmed();
}

} // namespace

Power currentPower() {
    return readFrom(kSupplyDir);
}

Power readFrom(const QString &supplyDir) {
    QDir dir(supplyDir);
    if (!dir.exists())
        return Power::Mains;

    // Collected first, then judged in two passes. Deciding as we go would
    // let directory order matter: a discharging battery listed before the
    // mains adapter would win, and a plugged-in laptop would look like it
    // was on battery.
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    // An online mains adapter settles it, wherever it appears in the
    // listing.
    for (const QFileInfo &entry : entries) {
        if (field(entry.filePath(), QStringLiteral("type")) == QStringLiteral("Mains")
            && field(entry.filePath(), QStringLiteral("online")) == QStringLiteral("1")) {
            return Power::Mains;
        }
    }

    for (const QFileInfo &entry : entries) {
        if (field(entry.filePath(), QStringLiteral("type")) != QStringLiteral("Battery"))
            continue;

        // Peripheral batteries -- a wireless keyboard or mouse -- are not
        // the machine's power source and must not be mistaken for one.
        if (field(entry.filePath(), QStringLiteral("scope")) == QStringLiteral("Device"))
            continue;

        if (field(entry.filePath(), QStringLiteral("status")) == QStringLiteral("Discharging"))
            return Power::Battery;
    }

    // No system battery, or one that is charging or full.
    return Power::Mains;
}
