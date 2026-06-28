#include "mainwindow.h"
#include "detail.h"

using namespace mw;

#include "canvas.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    event->accept();
}

void MainWindow::updateWindowTitle() {
    const QString name =
        m_currentFilePath.isEmpty() ? tr("Untitled") : QFileInfo(m_currentFilePath).fileName();
    const QString exampleTag = m_isExampleDocument ? tr(" (Example)") : QString();
    setWindowTitle(tr("%1%2 [*] - LGM").arg(name, exampleTag));
    setWindowModified(!m_scene->undoStack()->isClean());
}

bool MainWindow::confirmDiscardChanges() {
    if (m_scene->undoStack()->isClean()) {
        return true;
    }
    const QMessageBox::StandardButton btn = QMessageBox::warning(
        this, tr("Unsaved Changes"),
        tr("The document has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (btn == QMessageBox::Cancel) {
        return false;
    }
    if (btn == QMessageBox::Save) {
        return fileSave();
    }
    return true;
}

void MainWindow::fileNew() {
    if (!confirmDiscardChanges()) {
        return;
    }
    m_clearingDocument = true;
    m_scene->clearDocument();
    m_clearingDocument = false;
    m_currentFilePath.clear();
    m_isExampleDocument = false;
    m_scene->undoStack()->setClean();
    updateWindowTitle();
    m_objectTree->clearSelection();
    updateObjectList();
    updatePropertyPanel();
    updateStateSpacePanel();
}

void MainWindow::fileOpen() {
    if (!confirmDiscardChanges()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open"), QString(), tr("Linear Graph Model (*.lgm);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    loadDocumentFromPath(path);
}

void MainWindow::fileOpenExample(const QString& path) {
    if (!confirmDiscardChanges()) {
        return;
    }
    loadDocumentFromPath(path, true);
}

void MainWindow::openDocument(const QString& path) {
    loadDocumentFromPath(path);
}

bool MainWindow::loadDocumentFromPath(const QString& path, bool fromExamplesMenu) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open Failed"), tr("Could not read %1.").arg(path));
        return false;
    }

    QString error;
    if (!m_scene->documentFromJson(file.readAll(), &error)) {
        QMessageBox::warning(this, tr("Open Failed"), error);
        return false;
    }

    const QString loadWarning = m_scene->takeLoadWarning();
    if (!loadWarning.isEmpty()) {
        QMessageBox::warning(this, tr("Open"), loadWarning);
    }

    m_currentFilePath = path;
    m_isExampleDocument = fromExamplesMenu || isExampleFilePath(path);
    m_scene->undoStack()->setClean();
    syncDefaultSystemTypeCombo(m_scene->defaultSystemType());
    updateWindowTitle();
    updateObjectList();
    updatePropertyPanel();
    updateStateSpacePanel();
    return true;
}

bool MainWindow::isExampleFilePath(const QString& path) const {
    if (path.isEmpty()) {
        return false;
    }
    const QString root = examplesDir();
    if (root.isEmpty()) {
        return false;
    }
    QString dir = QFileInfo(root).absoluteFilePath();
    if (dir.isEmpty()) {
        return false;
    }
    if (!dir.endsWith(QLatin1Char('/')) && !dir.endsWith(QLatin1Char('\\'))) {
        dir += QDir::separator();
    }
    const QString file = QFileInfo(path).absoluteFilePath();
    if (file.isEmpty()) {
        return false;
    }
    return file.startsWith(dir, Qt::CaseInsensitive);
}

bool MainWindow::fileSave() {
    if (m_currentFilePath.isEmpty() || m_isExampleDocument) {
        return fileSaveAs();
    }
    return writeDocument(m_currentFilePath);
}

bool MainWindow::fileSaveAs() {
    const QString initialPath = m_isExampleDocument ? QString() : m_currentFilePath;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save As"), initialPath,
        tr("Linear Graph Model (*.lgm);;All Files (*)"));
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QStringLiteral(".lgm"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".lgm");
    }
    if (!writeDocument(path)) {
        return false;
    }
    m_currentFilePath = path;
    m_isExampleDocument = false;
    updateWindowTitle();
    return true;
}

bool MainWindow::writeDocument(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Save Failed"), tr("Could not write %1.").arg(path));
        return false;
    }
    if (file.write(m_scene->documentToJson()) == -1) {
        QMessageBox::warning(this, tr("Save Failed"), tr("Could not write %1.").arg(path));
        return false;
    }
    m_scene->undoStack()->setClean();
    updateWindowTitle();
    return true;
}
