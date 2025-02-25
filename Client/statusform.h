#ifndef STATUSFORM_H
#define STATUSFORM_H

#include <QDialog>
#include <QTcpSocket>
#include <memory> // For smart pointers
#include <QTime> // Added for QTime
#include "info.h"
#include <QCamera>
#include <QVideoWidget>
#include <QImageCapture>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QVideoSink>

// Forward declaration of MainWindow class
class MainWindow;

namespace Ui {
class statusForm;
}

class statusForm : public QDialog
{
    Q_OBJECT

public:
    // Constructor with default parameters
    explicit statusForm(QWidget *parent = nullptr, MainWindow *mainWindow = nullptr);

    // Destructor
    ~statusForm();

    // Make requestUserData public
    void requestUserData(const QString &requestType, const QString &username = QString());

    QString currentUsername;

    void reconnectToServer();

private slots:
    // Slot for handling exit button click
    void on_btnExit_clicked();

    // Slot for handling socket errors
    void onSocketError(QAbstractSocket::SocketError socketError);

    // Slot for handling socket ready read
    void onSocketReadyRead();
  
    void handleUsersData(const QJsonArray &users); // Updated to take one argument
    void on_btnInfo_clicked(); // Added this function
    void handleInfoData(const QJsonObject &info);
    void on_btnAdd_clicked();
    void on_btnUpdate_clicked();
    void on_lblAvatar_clicked();
    void startCamera();
    void captureImage();
    void setAvatarForUser(const QString &username);
    void on_btnCreateIntroVideo_clicked();
    void on_btnUpdateIntroVideo_clicked();
    void startRecordingIntroVideo();
    void sendExitRequest();
    void closeEvent(QCloseEvent *event);
    void on_btnPoints_clicked();
    void handleUserPoints(const QString &username, int points);
    void onImageSaved(int id, const QString &fileName);
    void on_btnPlay_clicked();
    void on_btnStop_clicked();
    void displayIntroVideo(const QString &username);
    void updateVideoFrame(const QVideoFrame &frame);
    void uploadFile(const QString &filePath, const QUrl &url);
    void sendFileToServer(const QString &filePath, const QUrl &serverUrl);
    void handleMediaPlayerError(QMediaPlayer::Error error);
private:
    std::unique_ptr<Ui::statusForm> ui;        // Smart pointer to the UI form
    MainWindow *mainWindow;                    // Pointer to the MainWindow
    std::unique_ptr<QTcpSocket> socket;        // Smart pointer to the QTcpSocket
    bool requestInProgress; // Added this line
    QCamera *camera;                           // Add this line
    QImageCapture *imageCapture;
    QDialog *cameraDialog; // Add this line
    QByteArray *buffer; // Added this line
    void handleServerResponse(const QJsonObject &jsonObj); // Added this line
    QMediaPlayer *mediaPlayer;
    QVideoWidget *videoWidget;
    QVideoSink *videoSink;

};

#endif // STATUSFORM_H