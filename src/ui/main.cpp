#include "mainwindow.h"

#include "app_settings.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LGM"));
    app.setApplicationVersion(QStringLiteral(LGM_VERSION));
    app.setOrganizationName("LGM");
    app.setWindowIcon(QIcon(":/app_logo.ico"));

    applyAppTheme(AppSettings::load().theme);

    MainWindow window;
    window.show();

    return app.exec();
}
