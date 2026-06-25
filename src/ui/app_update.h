#pragma once

class QWidget;

// ponytail: GitHub API check only; opens release page for manual download.
void checkForUpdates(QWidget* parent, bool silentWhenCurrent = false);
