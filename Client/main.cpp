#include "mainwindow.h"
#include <QApplication>
#include <QProcess>
#include <QVector>
#include <QDir>
#include <QDebug>
#include <QProcessEnvironment>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
