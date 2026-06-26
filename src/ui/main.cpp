#include "mainwindow.h"

#include "app_settings.h"

#include <QApplication>
#include <QIcon>

#include "app_log.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LGM"));
    app.setApplicationVersion(QStringLiteral(LGM_VERSION));
    app.setOrganizationName("LGM");
    app.setWindowIcon(QIcon(":/app_logo.ico"));

    applyAppTheme(AppSettings::load().theme);

    AppLog::install();

    MainWindow window;
    window.showMaximized();

    return app.exec();
}
