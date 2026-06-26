#pragma once

#include "app_settings.h"
#include "canvas.h"

#include <QMainWindow>

class QAction;
class QActionGroup;
class QShortcut;
class QComboBox;
class QDockWidget;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class QTableWidgetItem;
class QTextEdit;
class QVBoxLayout;
class SettingsWindow;
class AnalyzeWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildMenuBar();
    void buildToolbar();
    void buildStatusBar();
    void buildDockPanels();
    void syncModeUi(GraphScene::Mode mode);
    void updateObjectList();
    void updatePropertyPanel();
    void updateStateSpacePanel();
    void syncObjectTreeSelection();
    void onObjectTreeSelectionChanged();
    void onObjectTreeItemChanged(QTreeWidgetItem* item, int column);
    void onPropertyTableItemChanged(QTableWidgetItem* item);
    void updateFlipBranchAction();
    static QString modeStatusText(GraphScene::Mode mode);
    void syncDefaultSystemTypeCombo(SystemType type);
    AppSettings currentSettings() const;
    void applySettings(const AppSettings& settings);
    void applyShortcuts(const AppSettings& settings);
    void showSettingsWindow();
    void showAnalyzeWindow();
    void showConsoleWindow();
    void refreshChromeTheme();
    void refreshToolIcons();
    void updateWindowTitle();
    bool confirmDiscardChanges();
    void fileNew();
    void fileOpen();
    void fileOpenExample(const QString& path);
    bool loadDocumentFromPath(const QString& path, bool fromExamplesMenu = false);
    bool fileSave();
    bool fileSaveAs();
    bool writeDocument(const QString& path);
    bool isExampleFilePath(const QString& path) const;

    GraphScene* m_scene = nullptr;
    GraphView* m_view = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QAction* m_selectAction = nullptr;
    QAction* m_addNodeAction = nullptr;
    QAction* m_addBranchAction = nullptr;
    QAction* m_addTwoPortAction = nullptr;
    QAction* m_analyzeAction = nullptr;
    QAction* m_consoleAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_flipBranchAction = nullptr;
    QAction* m_mergeNodesAction = nullptr;
    QAction* m_toggleTwoPortAction = nullptr;
    QComboBox* m_defaultSystemTypeCombo = nullptr;
    bool m_updatingDomainCombo = false;
    AppTheme m_theme = AppTheme::System;
    SettingsWindow* m_settingsWindow = nullptr;
    AnalyzeWindow* m_analyzePanel = nullptr;
    QString m_currentFilePath;
    bool m_isExampleDocument = false;
    
    QDockWidget* m_propertyDock = nullptr;
    QDockWidget* m_objectListDock = nullptr;
    QDockWidget* m_analyzeDock = nullptr;
    QDockWidget* m_consoleDock = nullptr;
    QDockWidget* m_stateSpaceDock = nullptr;
    QTableWidget* m_propertyTable = nullptr;
    QTreeWidget* m_objectTree = nullptr;
    QTextEdit* m_consoleText = nullptr;
    QWidget* m_stateSpaceScrollContent = nullptr;
    QVBoxLayout* m_stateSpaceLayout = nullptr;
    bool m_syncingObjectTree = false;
    bool m_blockSceneSelectionSync = false;
    bool m_updatingPropertyPanel = false;
    bool m_clearingDocument = false;
    void* m_propertyTargetPtr = nullptr;
    int m_propertyTargetKind = -1;
    QShortcut* m_applyManualTreeReturnShortcut = nullptr;
    QShortcut* m_applyManualTreeEnterShortcut = nullptr;
    QShortcut* m_cancelManualTreeShortcut = nullptr;
};
