#include "guide_window.h"

#include <QDesktopServices>
#include <QFile>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

GuideWindow::GuideWindow(const QString& title, const QString& resourcePath, QWidget* parent)
    : QWidget(parent, Qt::Window) {
    setWindowTitle(title);
    resize(820, 640);

    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(false);
    browser->setReadOnly(true);

    QFile file(resourcePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        browser->setMarkdown(QString::fromUtf8(file.readAll()));
    } else {
        browser->setMarkdown(tr("*Guide not found:* `%1`").arg(resourcePath));
    }

    connect(browser, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        const QString scheme = url.scheme().toLower();
        if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
            QDesktopServices::openUrl(url);
        }
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(browser);
}

void GuideWindow::showGuide(const QString& title, const QString& resourcePath, QWidget* parent) {
    auto* window = new GuideWindow(title, resourcePath, parent);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    window->raise();
    window->activateWindow();
}
