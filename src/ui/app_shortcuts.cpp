#include "app_shortcuts.h"

namespace {

struct RawShortcutDef {
    const char* id;
    const char* label;
    const char* keys;
};

// ponytail: plain C strings only — QKeySequence must not be built before QApplication exists
const RawShortcutDef kRaw[] = {
    {"file.new", QT_TR_NOOP("New"), "Ctrl+N"},
    {"file.open", QT_TR_NOOP("Open"), "Ctrl+O"},
    {"file.save", QT_TR_NOOP("Save"), "Ctrl+S"},
    {"file.saveAs", QT_TR_NOOP("Save As"), "Ctrl+Shift+S"},
    {"file.quit", QT_TR_NOOP("Quit"), "Ctrl+Q"},
    {"edit.undo", QT_TR_NOOP("Undo"), "Ctrl+Z"},
    {"edit.redo", QT_TR_NOOP("Redo"), "Ctrl+Y"},
    {"edit.selectAll", QT_TR_NOOP("Select All"), "Ctrl+A"},
    {"edit.delete", QT_TR_NOOP("Delete"), "Del"},
    {"edit.flipBranch", QT_TR_NOOP("Flip Branch"), "F"},
    {"edit.mergeNodes", QT_TR_NOOP("Combine Nodes"), "M"},
    {"edit.toggleTwoPort", QT_TR_NOOP("Toggle Two-Port Kind"), "T"},
    {"tool.select", QT_TR_NOOP("Select Tool"), "Esc"},
    {"tool.addNode", QT_TR_NOOP("Add Node"), "N"},
    {"tool.addBranch", QT_TR_NOOP("Add Branch"), "B"},
    {"tool.addTwoPort", QT_TR_NOOP("Add Two-Port"), "P"},
    {"view.home", QT_TR_NOOP("Go Home"), "Home"},
    {"view.zoomIn", QT_TR_NOOP("Zoom In"), "Ctrl++"},
    {"view.zoomOut", QT_TR_NOOP("Zoom Out"), "Ctrl+-"},
    {"analysis.normalTree", QT_TR_NOOP("Find Normal Tree"), "Ctrl+Shift+T"},
    {"analysis.selectNormalTree", QT_TR_NOOP("Select Normal Tree"), "Ctrl+Alt+T"},
    {"analysis.stateSpace", QT_TR_NOOP("Compute State Space"), "Ctrl+Shift+S"},
};

const RawShortcutDef* findRaw(const QString& id) {
    for (const RawShortcutDef& raw : kRaw) {
        if (id == QLatin1String(raw.id)) {
            return &raw;
        }
    }
    return nullptr;
}

}  // namespace

const QVector<ShortcutDef>& AppShortcuts::defs() {
    static QVector<ShortcutDef> defs;
    if (defs.isEmpty()) {
        defs.reserve(static_cast<int>(sizeof(kRaw) / sizeof(kRaw[0])));
        for (const RawShortcutDef& raw : kRaw) {
            defs.push_back(
                {raw.id, raw.label, QKeySequence(QString::fromLatin1(raw.keys))});
        }
    }
    return defs;
}

bool AppShortcuts::isKnown(const QString& id) {
    return findRaw(id) != nullptr;
}

QKeySequence AppShortcuts::defaultFor(const QString& id) {
    if (const RawShortcutDef* raw = findRaw(id)) {
        return QKeySequence(QString::fromLatin1(raw->keys));
    }
    return {};
}
