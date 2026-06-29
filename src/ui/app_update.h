#pragma once

class QWidget;

// ponytail: GitHub API check only; opens release page for manual download.
// respectDeferrals: skip ignored / remind-later versions (startup check only).
void checkForUpdates(QWidget* parent, bool silentWhenCurrent = false, bool respectDeferrals = false);
