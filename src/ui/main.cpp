#include "mainwindow.h"

#include "app_settings.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Linear Graph Modeling");
    app.setOrganizationName("LinearGraphModeling");

    applyAppTheme(AppSettings::load().theme);

    MainWindow window;
    window.show();

    return app.exec();
}
