#include <QApplication>
#include <QIcon>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Nexus");
    QApplication::setApplicationName("Nexus Launcher");
    QApplication::setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/nexus/nexus_launcher_asset.png"));

    MainWindow w;
    w.show();
    return app.exec();
}
