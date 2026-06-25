#include "app_update.h"

#include <QApplication>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QAbstractButton>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVersionNumber>

namespace {

#ifndef LGM_GITHUB_REPO
#define LGM_GITHUB_REPO "FurkanKaraketir/LGM"
#endif

QVersionNumber parseReleaseVersion(QString tag) {
    if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        tag.remove(0, 1);
    }
    return QVersionNumber::fromString(tag);
}

} // namespace

void checkForUpdates(QWidget* parent, bool silentWhenCurrent) {
    auto* network = new QNetworkAccessManager(parent);
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                       .arg(QStringLiteral(LGM_GITHUB_REPO)));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGM/%1").arg(QApplication::applicationVersion()));

    QNetworkReply* reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, parent, [parent, reply, network, silentWhenCurrent]() {
        reply->deleteLater();
        network->deleteLater();

        const auto warn = [parent, silentWhenCurrent](const QString& text) {
            if (!silentWhenCurrent) {
                QMessageBox::warning(parent, QObject::tr("Check for Updates"), text);
            }
        };

        if (reply->error() != QNetworkReply::NoError) {
            warn(QObject::tr("Could not reach GitHub: %1").arg(reply->errorString()));
            return;
        }

        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value(QStringLiteral("tag_name")).toString();
        const QString notes = release.value(QStringLiteral("body")).toString();
        const QString pageUrl = release.value(QStringLiteral("html_url")).toString();

        if (tag.isEmpty()) {
            warn(QObject::tr("No releases found yet."));
            return;
        }

        const QVersionNumber latest = parseReleaseVersion(tag);
        const QVersionNumber current =
            QVersionNumber::fromString(QApplication::applicationVersion());

        if (latest <= current) {
            if (!silentWhenCurrent) {
                QMessageBox::information(
                    parent, QObject::tr("Check for Updates"),
                    QObject::tr("You have the latest version (%1).")
                        .arg(QApplication::applicationVersion()));
            }
            return;
        }

        auto* box = new QMessageBox(QMessageBox::Information, QObject::tr("Update Available"),
                                    QObject::tr("Version %1 is available (you have %2).")
                                        .arg(tag, QApplication::applicationVersion()),
                                    QMessageBox::NoButton, parent);
        if (!notes.isEmpty()) {
            box->setDetailedText(notes);
        }

        QPushButton* downloadButton =
            box->addButton(QObject::tr("Download"), QMessageBox::AcceptRole);
        box->addButton(QMessageBox::Close);
        box->setDefaultButton(downloadButton);

        QObject::connect(box, &QMessageBox::finished, box, [box, downloadButton, pageUrl](int) {
            if (box->clickedButton() == static_cast<QAbstractButton*>(downloadButton)) {
                QDesktopServices::openUrl(QUrl(pageUrl));
            }
            box->deleteLater();
        });
        box->open();
    });
}
