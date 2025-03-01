#include "server.h"
#include <QApplication>
#include <QLoggingCategory>
#include <QFile>
#include <QDateTime>
#include <QDir>

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString txt;
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    
    switch (type) {
    case QtDebugMsg:
        txt = QString("%1 [Debug] %2").arg(timestamp, msg);
        break;
    case QtInfoMsg:
        txt = QString("%1 [Info] %2").arg(timestamp, msg);
        break;
    case QtWarningMsg:
        txt = QString("%1 [Warning] %2").arg(timestamp, msg);
        break;
    case QtCriticalMsg:
        txt = QString("%1 [Critical] %2").arg(timestamp, msg);
        break;
    case QtFatalMsg:
        txt = QString("%1 [Fatal] %2").arg(timestamp, msg);
        break;
    }

    QString logPath = QCoreApplication::applicationDirPath() + "/server.log";
    QFile outFile(logPath);
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream ts(&outFile);
    ts << txt << Qt::endl;
    outFile.close();

    // Also output to stderr for immediate feedback
    fprintf(stderr, "%s\n", qPrintable(txt));
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // Install the custom message handler
    qInstallMessageHandler(messageHandler);
    
    qInfo() << "Application starting...";
    qInfo() << "Application path:" << QCoreApplication::applicationDirPath();
    
    // Create log directory if it doesn't exist
    QDir appDir(QCoreApplication::applicationDirPath());
    if (!appDir.exists("logs")) {
        if (!appDir.mkdir("logs")) {
            qCritical() << "Failed to create logs directory";
        }
    }
    
    try {
        qInfo() << "Creating server instance...";
        server w;
        qInfo() << "Showing server window...";
        w.show();
        
        // Connect to handle server errors
        QObject::connect(&w, &server::serverError, [](const QString &error) {
            qCritical() << "Server error occurred:" << error;
        });
        
        qInfo() << "Entering application event loop...";
        return a.exec();
    } catch (const std::exception& e) {
        qCritical() << "Unhandled exception:" << e.what();
        return 1;
    } catch (...) {
        qCritical() << "Unknown exception occurred";
        return 1;
    }
}

