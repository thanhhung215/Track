#include <QApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QDataStream>
#include "server.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    server w;
    w.show();
    return a.exec();
}

