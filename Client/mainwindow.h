#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include "statusform.h"
#include <registerform.h>


// Forward declaration of the Ui namespace
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Constructor and Destructor
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Getter for username
    QString getUsername() const;

    // Method to disconnect from the server
    void disconnectFromServer();

    // Method to send data to the server
    void sendData(const QByteArray &data);
    void on_lblRegister_clicked();

signals:
    // Signal to notify when data is received
    void dataReceived(const QByteArray &data);

private slots:
    // Slot to handle server connection
    void connectToServer(const QHostAddress &host, int port);

    // Slots for socket events
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError socketError);
    void readData();

    // Slot for login button click
    void on_btnLogin_clicked();

    // Slot to send status request
    //void sendStatusRequest();

    // Slot to handle login response
    void handleLoginResponse(const QString &response); // Added this line

private:
    // UI pointer
    Ui::MainWindow *ui;

    // Socket pointer
    QTcpSocket *socket;
};

#endif // MAINWINDOW_H
