// Whether the machine is running on mains power, ported from
// omabook-core/src/ai/power.rs.
//
// Read straight from sysfs rather than through UPower: it is a handful of
// files, needs no session bus, and works in a headless test.
#pragma once

#include <QString>

// Whether the machine is on mains power.
enum class Power { Mains, Battery };

// Current power source, read from /sys/class/power_supply. A machine with
// no system battery -- a desktop, or this mini PC -- reports Mains, so
// background work is not blocked by the absence of a battery.
Power currentPower();

// Same lookup over an arbitrary directory shaped like
// /sys/class/power_supply, so a test can point it at a QTemporaryDir
// fixture and get a predictable answer without touching the real
// filesystem. Declared at namespace scope rather than as a class member --
// there is no natural class to hang it on here (CLAUDE.md, "Pure logic
// goes in static member functions").
Power readFrom(const QString &supplyDir);
