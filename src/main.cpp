#include "mainwindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Linear Graph Modeling");
    app.setOrganizationName("LinearGraphModeling");

    MainWindow window;
    window.show();

    return app.exec();
}
