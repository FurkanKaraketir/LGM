#pragma once

#include <QKeySequence>
#include <QString>
#include <QVector>

struct ShortcutDef {
    const char* id;
    const char* label;
    QKeySequence defaultSequence;
};

class AppShortcuts {
public:
    static const QVector<ShortcutDef>& defs();
    static bool isKnown(const QString& id);
    static QKeySequence defaultFor(const QString& id);
};
