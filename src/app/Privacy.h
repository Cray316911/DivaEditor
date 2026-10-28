// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCoreApplication>

// `DivaStudio --privacy` is for documentation screenshots: settings go to a throw-away folder
// and machine-specific details (GPU name, refresh rate, Windows build) show generic text.
inline bool privacyMode()
{
    return QCoreApplication::instance() && QCoreApplication::instance()->property("divaPrivacy").toBool();
}
