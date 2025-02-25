#ifndef SERVER_H
#define SERVER_H

#include <QMainWindow>
#include <QTcpServer>
#include <QJsonArray>
#include <QHttpServer>
#include <QObject>

QT_BEGIN_NAMESPACE
namespace Ui { class server; }
QT_END_NAMESPACE

class server : public QMainWindow
{
    Q_OBJECT

public:
    server(QWidget *parent = nullptr);
    ~server();

private slots:
    void onNewConnection();
    void on_btnView_clicked();
    void handleClientData(QTcpSocket* socket);
    bool checkCredentials(const QString &username, const QString &password);
    void getUserWithClosestTime(QTcpSocket* socket);
    void saveUserStatus(const QString &username, const QString &status, const QString &time);
    void on_btnSubmit_clicked();
    void saveInfoData(const QString &username, const QJsonObject &infoData, QTcpSocket* socket);
    void updateClock();
    void showTimesheet();
    void showCurrentPoints(const QString &username, QTcpSocket* socket);
    void setupHttpServer();
    void handleImageUpload(const QHttpServerRequest &request);
    void handleVideoUpload(const QHttpServerRequest &request);
    void on_btnCreate_clicked();
    void on_btnDrop_clicked();
    void handleClientRegister(QTcpSocket* socket, const QJsonObject &obj);
    void saveJsonFile(const QJsonObject &data);
    void on_btnChange_clicked();
private:
    Ui::server *ui;
    QTcpServer *tcpServer;
    QJsonObject loadJsonFile();
    QJsonArray handleUserStatusRequest();
    QString currentUsername;
    QHttpServer *httpServer;
};

#endif // SERVER_H
