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
#include <QSettings>
#include <QUrl>
#include <QVersionNumber>

#include <cassert>

namespace {

#ifndef LGM_GITHUB_REPO
#define LGM_GITHUB_REPO "FurkanKaraketir/LGM"
#endif

QString s_remindLaterVersion; // ponytail: session-only; cleared on next launch

QVersionNumber parseReleaseVersion(QString tag) {
    if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        tag.remove(0, 1);
    }
    return QVersionNumber::fromString(tag);
}

QString ignoredUpdateVersion() {
    return QSettings().value(QStringLiteral("update/ignoredVersion")).toString();
}

void setIgnoredUpdateVersion(const QString& tag) {
    QSettings().setValue(QStringLiteral("update/ignoredVersion"), tag);
}

bool shouldSkipDeferred(const QString& tag, bool respectDeferrals) {
    if (!respectDeferrals) {
        return false;
    }
    return tag == ignoredUpdateVersion() || tag == s_remindLaterVersion;
}

} // namespace

void checkForUpdates(QWidget* parent, bool silentWhenCurrent, bool respectDeferrals) {
    auto* network = new QNetworkAccessManager(parent);
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                       .arg(QStringLiteral(LGM_GITHUB_REPO)));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGM/%1").arg(QApplication::applicationVersion()));

    QNetworkReply* reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, parent,
                     [parent, reply, network, silentWhenCurrent, respectDeferrals]() {
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

                         if (shouldSkipDeferred(tag, respectDeferrals)) {
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
                         QPushButton* remindButton =
                             box->addButton(QObject::tr("Remind Me Later"), QMessageBox::RejectRole);
                         QPushButton* ignoreButton =
                             box->addButton(QObject::tr("Ignore This Update"), QMessageBox::DestructiveRole);
                         box->setDefaultButton(downloadButton);
                         box->setEscapeButton(remindButton);

                         QObject::connect(box, &QMessageBox::finished, box,
                                          [box, downloadButton, remindButton, ignoreButton, tag, pageUrl](int) {
                                              QAbstractButton* clicked = box->clickedButton();
                                              if (clicked == downloadButton) {
                                                  QDesktopServices::openUrl(QUrl(pageUrl));
                                              } else if (clicked == ignoreButton) {
                                                  setIgnoredUpdateVersion(tag);
                                              } else {
                                                  s_remindLaterVersion = tag;
                                              }
                                              box->deleteLater();
                                          });
                         box->open();
                     });
}

#ifndef NDEBUG
namespace {
struct AppUpdateSelfCheck {
    AppUpdateSelfCheck() {
        assert(parseReleaseVersion(QStringLiteral("v1.2.3")) == QVersionNumber(1, 2, 3));
        assert(parseReleaseVersion(QStringLiteral("0.2.0")) == QVersionNumber(0, 2, 0));
    }
};
const AppUpdateSelfCheck kAppUpdateSelfCheck;
} // namespace
#endif
