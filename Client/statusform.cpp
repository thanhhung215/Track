#include "statusform.h"
#include "ui_statusform.h"
#include "mainwindow.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QMessageBox>
#include <QHostAddress>
#include <QJsonArray>
#include <QTimer>
#include <QTime>
#include "info.h"
#include <QCamera>
#include <QVideoWidget>
#include <QImageCapture>
#include <QFileDialog>
#include "clickablelabel.h"
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QMediaCaptureSession>
#include <QScreen>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QCryptographicHash>
#include <QBuffer>
#include <QMediaRecorder>
#include <QCloseEvent>
#include <QTableWidget>
#include <QThread>
#include <QVideoSink>
#include <QFile>
#include <functional>
#include <QImageReader>

// Constructor for statusForm
statusForm::statusForm(QWidget *parent, MainWindow *mainWindow)
    : QDialog(parent)
    , ui(new Ui::statusForm)
    , mainWindow(mainWindow)
    , socket(new QTcpSocket(this))
    , requestInProgress(false)
    , camera(nullptr)
    , imageCapture(nullptr)
    , cameraDialog(nullptr)
    , buffer(new QByteArray())
    , mediaPlayer(new QMediaPlayer(this))
    , videoWidget(new QVideoWidget(this))
    , videoSink(new QVideoSink(this))
{
    ui->setupUi(this);
    mediaPlayer->setVideoSink(videoSink);
    displayIntroVideo(mainWindow->getUsername());

    // Connect frame updates to a slot
    connect(videoSink, &QVideoSink::videoFrameChanged, this, &statusForm::updateVideoFrame);
    connect(mediaPlayer, &QMediaPlayer::errorOccurred, this, &statusForm::handleMediaPlayerError);

    // Connect the exit button to the on_btnExit_clicked slot
    connect(ui->btnExit, &QPushButton::clicked, this, &statusForm::on_btnExit_clicked);

    // Connect the info button to the on_btnInfo_clicked slot
    connect(ui->btnInfo, &QPushButton::clicked, this, &statusForm::on_btnInfo_clicked);

    // Connect the add button to the on_btnAdd_clicked slot
    connect(ui->btnAdd, &QPushButton::clicked, this, &statusForm::on_btnAdd_clicked);

    // Connect the create intro video button to the on_btnCreateIntroVideo_clicked slot
    connect(ui->btnCreateIntroVideo, &QPushButton::clicked, this, &statusForm::on_btnCreateIntroVideo_clicked);
    connect(ui->btnUpdateIntroVideo, &QPushButton::clicked, this, &statusForm::on_btnUpdateIntroVideo_clicked);
    // Connect socket signals for error handling
    connect(socket.get(), &QTcpSocket::errorOccurred, this, &statusForm::onSocketError);
    connect(socket.get(), &QTcpSocket::readyRead, this, &statusForm::onSocketReadyRead);

    // Connect the update button to the on_btnUpdate_clicked slot
    connect(ui->btnUpdate, &QPushButton::clicked, this, &statusForm::on_btnUpdate_clicked);
    // Connect the points button to the on_btnPoints_clicked slot
    connect(ui->btnPoints, &QPushButton::clicked, this, &statusForm::on_btnPoints_clicked);

    // Connect to server
    socket->connectToHost(QHostAddress("127.0.0.1"), 1234);
    currentUsername = mainWindow->getUsername();
    setAvatarForUser(currentUsername);
    if (!socket->waitForConnected(5000)) {
        qDebug() << "statusForm::statusForm: Failed to connect to server.";
    } else {
        qDebug() << "statusForm::statusForm: Connected to server.";
        requestUserData("currentLoginUser");
    }

    // Connect lblAvatar click event to slot
    connect(ui->lblAvatar, &ClickableLabel::clicked, this, &statusForm::on_lblAvatar_clicked);

    // Connect the play button to the on_btnPlay_clicked slot
    connect(ui->btnPlay, &QPushButton::clicked, this, &statusForm::on_btnPlay_clicked);

    // Connect the stop button to the on_btnStop_clicked slot
    connect(ui->btnStop, &QPushButton::clicked, this, &statusForm::on_btnStop_clicked);
    

    // Connect socket disconnected signal
    connect(socket.get(), &QTcpSocket::disconnected, this, &statusForm::reconnectToServer);
}

// Slot to handle media player errors
void statusForm::handleMediaPlayerError(QMediaPlayer::Error error) {
    qDebug() << "Media Player Error:" << mediaPlayer->errorString();
    QMessageBox::critical(this, "Playback Error", "Error occurred during video playback: " + mediaPlayer->errorString());
}
void statusForm::uploadFile(const QString &filePath, const QUrl &url) {
    if (!QFile::exists(filePath)) {
        QMessageBox::warning(nullptr, "Upload Error", "File does not exist.");
        return;
    }

    QFile *file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(nullptr, "Upload Error", "Could not open file for reading.");
        return;
    }

    QByteArray fileData = file->readAll(); // Đọc dữ liệu file bằng cách sử dụng toán tử '->'
    QByteArray base64Data = fileData.toBase64(); // Mã hóa dữ liệu thành Base64

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"" + currentUsername + "\"; filename=\"" + file->fileName() + "\""));
    filePart.setBody(base64Data); // Sử dụng dữ liệu đã mã hóa Base64

    multiPart->append(filePart);

    QNetworkRequest request(url);
    QNetworkAccessManager *manager = new QNetworkAccessManager();
    QNetworkReply *reply = manager->post(request, multiPart);
    multiPart->setParent(reply); // delete the multiPart with the reply

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QMessageBox::information(nullptr, "Upload Success", "The file was uploaded successfully!");
        } else {
            QMessageBox::critical(nullptr, "Upload Failed", reply->errorString());
        }
        reply->deleteLater();
    });

    file->close(); // Đóng file sau khi đã đọc xong
    delete file; // Giải phóng bộ nhớ cho đối tượng file
}

void statusForm::sendFileToServer(const QString &filePath, const QUrl &serverUrl) {
    QFile *file = new QFile(filePath);
    if (!file->exists()) {
        QMessageBox::warning(this, "File Error", "File does not exist: " + filePath);
        return;
    }

    if (!file->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "File Error", "Could not open file for reading: " + filePath);
        return;
    }

    // Prepare multipart/form-data request
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"file\"; filename=\"" + file->fileName() + "\""));
    filePart.setBodyDevice(file);
    file->setParent(multiPart); // the file will be deleted automatically with the multiPart

    multiPart->append(filePart);

    // Create network request
    QNetworkRequest request(serverUrl);
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkReply *reply = manager->post(request, multiPart);
    multiPart->setParent(reply); // ensure the multiPart is deleted with the reply

    // Handle reply from server
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        if (reply->error() == QNetworkReply::NoError) {
            QMessageBox::information(this, "Upload Success", "The file was uploaded successfully!");
        } else {
            QMessageBox::critical(this, "Upload Failed", reply->errorString());
        }
        reply->deleteLater();
        manager->deleteLater();
        
        // Reconnect to the server after upload
        reconnectToServer();
    });
}

// Slot for handling the play button click
void statusForm::on_btnPlay_clicked() {
    disconnect(ui->btnPlay, &QPushButton::clicked, this, &statusForm::on_btnPlay_clicked);

    mediaPlayer->play();
    qDebug() << "Video playback started.";
}

// Slot for handling the stop button click
void statusForm::on_btnStop_clicked() {
    disconnect(ui->btnStop, &QPushButton::clicked, this, &statusForm::on_btnStop_clicked);

    mediaPlayer->pause();
    qDebug() << "Video playback paused.";
}
// Function to display the intro video for a user
// Function to display the intro video for a user
void statusForm::displayIntroVideo(const QString &username) {
    QString videoPath = QDir::currentPath() + "/intro/" + username + ".mp4";
    mediaPlayer->setSource(QUrl::fromLocalFile(videoPath));
    qDebug() << "Setting video source to:" << videoPath;
    mediaPlayer->setSource(QUrl::fromLocalFile(videoPath));
}

void statusForm::updateVideoFrame(const QVideoFrame &frame) {
    if (!frame.isValid()) return;

    QImage image = frame.toImage();
    QPixmap pixmap = QPixmap::fromImage(image);
    ui->lblDisplayIntroVideo->setPixmap(pixmap.scaled(ui->lblDisplayIntroVideo->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
// Slot for handling the update intro video button click
void statusForm::on_btnUpdateIntroVideo_clicked() {
    disconnect(ui->btnUpdateIntroVideo, &QPushButton::clicked, this, &statusForm::on_btnUpdateIntroVideo_clicked);


    QString filePath = QDir::currentPath() + "/intro/" + currentUsername + ".mp4";

    if (!QFile::exists(filePath)) {
        QMessageBox::information(this, "Intro Video", "Intro Video does not exist. Please create one first.");

        return;

    }


    startRecordingIntroVideo();
}

void statusForm::on_btnCreateIntroVideo_clicked() {
    disconnect(ui->btnCreateIntroVideo, &QPushButton::clicked, this, &statusForm::on_btnCreateIntroVideo_clicked);


    QString filePath = QDir::currentPath() + "/intro/" + currentUsername + ".mp4";
    if (QFile::exists(filePath)) {

        QMessageBox::information(this, "Intro Video", "Intro Video already exists. If you want to change, please press the 'Change' button.");

        return;

    }


    startRecordingIntroVideo();

}

// Function to start recording intro video
void statusForm::startRecordingIntroVideo() {
    camera = new QCamera(this);
    QMediaRecorder *mediaRecorder = new QMediaRecorder(this); // Initialize mediaRecorder with camera

    QMediaCaptureSession *captureSession = new QMediaCaptureSession;
    captureSession->setCamera(camera);
    captureSession->setRecorder(mediaRecorder);

    QVideoWidget *viewfinder = new QVideoWidget(this);
    viewfinder->setFixedSize(640, 480); // Set fixed size for the video widget
    captureSession->setVideoOutput(viewfinder);
    viewfinder->show();

    QPushButton *recordButton = new QPushButton("Record", this);
    QPushButton *stopButton = new QPushButton("Stop", this);
    stopButton->setEnabled(false); // Disable stop button initially

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(viewfinder);
    //layout->addWidget(recordButton);
    //layout->addWidget(stopButton);
    //layout->addStretch(); // Add stretch to push buttons to the top
    // Create a new horizontal layout for buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(); // Add stretch to push buttons to the center
    buttonLayout->addWidget(recordButton);
    buttonLayout->addWidget(stopButton);
    buttonLayout->addStretch(); // Add stretch to push buttons to the center
    layout->addLayout(buttonLayout);

    cameraDialog = new QDialog(this); // Initialize cameraDialog
    cameraDialog->setLayout(layout);
    cameraDialog->setWindowTitle("Record Intro Video");
    cameraDialog->setMinimumSize(660, 540); // Set minimum size for the dialog
    cameraDialog->setMaximumSize(660, 540); // Set maximum size for the dialog

    // Adjust the size constraints to fit within the available screen space
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect availableGeometry = screen->availableGeometry();
    cameraDialog->setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, cameraDialog->size(), availableGeometry));

    // Connect record button to start recording
    connect(recordButton, &QPushButton::clicked, this, [this, mediaRecorder, recordButton, stopButton]() {
        QString filePath = QDir::currentPath() + "/intro/" + currentUsername + ".mp4";

        // Create directory if it doesn't exist
        QDir dir(QDir::currentPath() + "/intro/");
        if (!dir.exists()) {
            if (!dir.mkpath(".")) {
                QMessageBox::warning(this, "Error", "Failed to create directory for intro videos.");
                return;
            }
        }

        mediaRecorder->setOutputLocation(QUrl::fromLocalFile(filePath));
        mediaRecorder->record();
        qDebug() << "Recording started";

        recordButton->setEnabled(false);
        stopButton->setEnabled(true);
    });

    // Connect stop button to stop recording
    connect(stopButton, &QPushButton::clicked, this, [this, mediaRecorder, recordButton, stopButton]() {
        mediaRecorder->stop();
        qDebug() << "Recording stopped";

        recordButton->setEnabled(true);
        stopButton->setEnabled(false);

        camera->stop();
        cameraDialog->accept(); // Close the camera dialog

        // Display the recorded video
        displayIntroVideo(currentUsername);
        QString filePath = QDir::currentPath() + "/intro/" + currentUsername + ".mp4";
        QUrl serverUrl("http://localhost:8080/intro"); // Assuming the endpoint is /upload
        sendFileToServer(filePath, serverUrl);
    });

    // Start the camera
    camera->start();
    qDebug() << "Camera started";

    cameraDialog->exec();
}
// Function to send intro video to the server

void statusForm::reconnectToServer() {
    if (socket->state() != QAbstractSocket::ConnectedState) {
        socket->connectToHost(QHostAddress("127.0.0.1"), 1234);
        if (socket->waitForConnected(5000)) {
            qDebug() << "Reconnected to server successfully.";
            requestUserData("currentLoginUser");
        } else {
            qDebug() << "Failed to reconnect to server:" << socket->errorString();
        }
    }
}

// Slot for handling the lblAvatar click
void statusForm::on_lblAvatar_clicked() {
    disconnect(ui->lblAvatar, &ClickableLabel::clicked, this, &statusForm::on_lblAvatar_clicked);
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Change Avatar", "Do you want to change Avatar?", QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        startCamera();
    }
}

// Function to start the camera
void statusForm::startCamera() {
    camera = new QCamera(this);
    imageCapture = new QImageCapture(this); // Initialize imageCapture with this

    QMediaCaptureSession *captureSession = new QMediaCaptureSession;
    captureSession->setCamera(camera);
    captureSession->setImageCapture(imageCapture);

    QVideoWidget *viewfinder = new QVideoWidget(this);
    viewfinder->setFixedSize(640, 480); // Set fixed size for the video widget
    captureSession->setVideoOutput(viewfinder);
    viewfinder->show();

    QPushButton *captureButton = new QPushButton("Capture", this);
    connect(captureButton, &QPushButton::clicked, this, &statusForm::captureImage);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(viewfinder);
    layout->addWidget(captureButton);

    cameraDialog = new QDialog(this); // Initialize cameraDialog
    cameraDialog->setLayout(layout);
    cameraDialog->setWindowTitle("Capture Avatar");
    cameraDialog->setMinimumSize(660, 540); // Set minimum size for the dialog
    cameraDialog->setMaximumSize(660, 540); // Set maximum size for the dialog

    // Adjust the size constraints to fit within the available screen space
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect availableGeometry = screen->availableGeometry();
    cameraDialog->setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, cameraDialog->size(), availableGeometry));

    // Start the camera
    camera->start();
    qDebug() << "Camera started";

    cameraDialog->exec();
}

void statusForm::captureImage() {
    QString filePath = QDir::currentPath() + "/avatar/" + currentUsername + ".jpg";

    // Create directory if it doesn't exist
    QDir dir(QDir::currentPath() + "/avatar/");
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            QMessageBox::warning(this, "Error", "Failed to create directory for avatar images.");
            return;
        }
    }

    // Remove old image if exists
    if (QFile::exists(filePath)) {
        if (!QFile::remove(filePath)) {
            QMessageBox::warning(this, "Error", "Failed to remove the old avatar image.");
            return;
        }
    }

    // Connect the imageSaved signal to a new slot
    connect(imageCapture, &QImageCapture::imageSaved, this, &statusForm::onImageSaved);

    // Capture the image and save it directly to the specified path
    imageCapture->captureToFile(filePath);
}

// Slot to handle image saved event
void statusForm::onImageSaved(int id, const QString &fileName) {
    Q_UNUSED(id);

    // Check if the file is ready to be opened
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open the captured image file.";
        QMessageBox::warning(this, "Error", "Failed to open the captured image file.");
        return;
    }
    file.close();

    // Use QImageReader to check the image
    QImageReader reader(fileName);
    if (!reader.canRead()) {
        qDebug() << "Cannot read the image file with QImageReader. Error:" << reader.errorString();
        QMessageBox::warning(this, "Error", "Cannot read the image file. Format might be unsupported.");
        return;
    }
    // Set captured image to lblAvatar
    QPixmap avatarPixmap;
    if (avatarPixmap.load(fileName)) {
        ui->lblAvatar->setPixmap(avatarPixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        qDebug() << "Avatar pixmap set successfully.";
        QMessageBox::information(this, "Success", "Avatar image saved successfully at " + fileName);
    } else {
        qDebug() << "Failed to load the captured image into QPixmap.";
        QMessageBox::warning(this, "Error", "Failed to load the captured image.");
    }

    // Call uploadFile instead of sendAvatarToServerFromFile
    QUrl serverUrl("http://localhost:8080/avatar"); // Assuming the endpoint is /upload
    uploadFile(fileName, serverUrl);

    // Ensure cameraDialog is closed properly without affecting statusForm
    if (cameraDialog) {
        cameraDialog->reject(); // Use reject() instead of accept() to make sure it does not emit the accepted signal
    }

    //camera->stop();
}


void statusForm::onSocketError(QAbstractSocket::SocketError socketError) {
    qDebug() << "Socket error occurred:" << socketError;
    QMessageBox::critical(this, "Socket Error", "An error occurred: " + socket->errorString());
}
void statusForm::setAvatarForUser(const QString &username) {
    QString filePath = QDir::currentPath() + "/avatar/" + username + ".jpg";
    QPixmap avatarPixmap;

    if (QFile::exists(filePath)) {
        avatarPixmap.load(filePath);
        qDebug() << "Avatar set for user:" << username;
    } else {
        avatarPixmap.load(":/icon/user.png");
        qDebug() << "Avatar file does not exist for user:" << username << ". Setting default avatar.";
    }

    ui->lblAvatar->setPixmap(avatarPixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// Function to request user data from the server
void statusForm::requestUserData(const QString &requestType, const QString &username) {
    if (socket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject requestData;
        requestData["request"] = requestType;
        if (!username.isEmpty()) {
            requestData["username"] = username; // Include the username in the request if provided
        }
        QJsonDocument doc(requestData);
        QByteArray message = doc.toJson(QJsonDocument::Compact);
        socket->write(message);
    } else {
        qDebug() << "statusForm::requestUserData: Socket is not connected.";
    }
}

// Slot to handle server responses
void statusForm::onSocketReadyRead() {
    buffer->append(socket->readAll());
    qDebug() << "statusForm::onSocketReadyRead: Buffer size:" << buffer->size();

    while (!buffer->isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(*buffer);
        if (doc.isNull()) {
            qDebug() << "statusForm::onSocketReadyRead: Incomplete JSON data, waiting for more data.";
            break;
        }

        QJsonObject jsonObj = doc.object();
        buffer->remove(0, doc.toJson(QJsonDocument::Compact).size());
        handleServerResponse(jsonObj);
    }
}

void statusForm::handleServerResponse(const QJsonObject &jsonObj) {
    if (jsonObj.contains("response")) {
        QString responseType = jsonObj["response"].toString();
        if (responseType == "currentLoginUser") {
            handleUsersData(jsonObj["users"].toArray());
        } else if (responseType == "responseInfo") {
            handleInfoData(jsonObj["info"].toObject()); // Handle responseInfo
        } else if (responseType == "infoEmpty") {
            QMessageBox::information(this, "Information", "The information is not available, please add your information.");
        } else if (responseType == "noInfo") {
            QMessageBox::information(this, "Information", "Information not available, please add information first."); // Added this line
        } else if (responseType == "updateInfo") {
            this->hide(); // Hide the statusForm
            Info *infoForm = new Info(this, this); // Create a new instance of info form
            infoForm->show(); // Show the info form

            // Connect the destroyed signal of infoForm to show statusForm again
            connect(infoForm, &QObject::destroyed, this, [this]() {
                this->show();
            });
        } else if (responseType == "Invalid JSON format") {
            QMessageBox::warning(this, "Error", "Invalid JSON format received from server.");
        } else if (responseType == "Login successful") {
            QMessageBox::information(this, "Success", "Login successful.");
        } else if (responseType == "Incorrect username or password") {
            QMessageBox::warning(this, "Error", "Incorrect username or password.");
        } else if (responseType == "Exit successful") {
            QMessageBox::information(this, "Success", "Exit successful.");
        } else if (responseType == "Invalid Client's data format (missing required fields)") {
            QMessageBox::warning(this, "Error", "Invalid Client's data format (missing required fields).");
        } else if (responseType == "Error: Couldn't open the status file") {
            QMessageBox::critical(this, "Error", "Couldn't open the status file.");
        } else if (responseType == "addInfo") {
            this->hide(); // Hide the statusForm
            Info *infoForm = new Info(this,this); // Create a new instance of info form
            infoForm->show(); // Show the info form

            // Connect the destroyed signal of infoForm to show statusForm again
            connect(infoForm, &QObject::destroyed, this, [this]() {
                this->show();
            });
        } else if (responseType == "Avatar uploaded successfully") {
            QMessageBox::critical(this, "Upload", "Avatar uploaded successfully");
            setAvatarForUser(currentUsername);
        } else if (responseType == "infoAvailable") {
            QMessageBox::information(this, "Information", "Information is already available, if you want to adjust please update.");
        } else if (responseType == "currentPoints") {
            handleUserPoints(jsonObj["username"].toString(), jsonObj["points"].toInt());
        }
    }
}
void statusForm::handleUserPoints(const QString &username, int points) {
    ui->tablePoints->setVisible(true);
    ui->tablePoints->setRowCount(1); // Assuming one row for the current user
    ui->tablePoints->setColumnCount(2); // Assuming two columns for username and points

    QTableWidgetItem *usernameItem = new QTableWidgetItem(username);
    QTableWidgetItem *pointsItem = new QTableWidgetItem(QString::number(points));

    ui->tablePoints->setItem(0, 0, usernameItem);
    ui->tablePoints->setItem(0, 1, pointsItem);
}

// Function to handle info data and populate tableInfo
void statusForm::handleInfoData(const QJsonObject &info) { // Change parameter type to QJsonObject
    ui->tableInfo->setVisible(true);
    ui->tableInfo->setRowCount(info.size());
    ui->tableInfo->setColumnCount(2); // Assuming two columns for key and value

    int row = 0;
    for (auto it = info.begin(); it != info.end(); ++it, ++row) {
        QString key = it.key();
        QString value = it.value().toString();

        QTableWidgetItem *keyItem = new QTableWidgetItem(key);
        QTableWidgetItem *valueItem = new QTableWidgetItem(value);

        ui->tableInfo->setItem(row, 0, keyItem);
        ui->tableInfo->setItem(row, 1, valueItem);
    }
}

void statusForm::handleUsersData(const QJsonArray &users) {
    const QString onlineStatusIconPath = ":/icon/icon/shape.png"; // Constant for online status icon path

    // Ensure tableWidget is visible and has the correct number of columns
    ui->tableWidget->setVisible(true);
    ui->tableWidget->setColumnCount(2); // Assuming 2 columns for username and status

    ui->tableWidget->setRowCount(users.size());
    for (int i = 0; i < users.size(); ++i) {
        QJsonObject user = users[i].toObject();
        QString username = user["username"].toString();
        QString status = user["status"].toString();

        qDebug() << "statusForm::handleUserStatusData: User" << i << ":" << user;

        QTableWidgetItem *usernameItem = new QTableWidgetItem(username);
        QTableWidgetItem *statusItem = new QTableWidgetItem();
        QIcon statusIcon(status == "online" ? ":/icon/icon/shape.png" : ":/icon/icon/circle.png");
        statusItem->setIcon(statusIcon);

        ui->tableWidget->setItem(i, 0, usernameItem);
        ui->tableWidget->setItem(i, 1, statusItem);
        // Set username in the UI
        ui->lblUsername->setText(username);
        // Adjust username label font size and position
        QFont font = ui->lblUsername->font();
        font.setPointSize(10); // Adjust font size to be suitable with avatar size
        ui->lblUsername->setFont(font);

        ui->lblUsername->setAlignment(Qt::AlignCenter); // Center align the username text

    }
}

// Slot for handling the info button click
void statusForm::on_btnInfo_clicked() {
    // Get the current username from the tableWidget
    disconnect(ui->btnInfo, &QPushButton::clicked, this, &statusForm::on_btnInfo_clicked);
    requestUserData("showInfo", currentUsername); // Pass the current username to the request
}

// Slot for handling the add button click
void statusForm::on_btnAdd_clicked() {
    disconnect(ui->btnAdd, &QPushButton::clicked, this, &statusForm::on_btnAdd_clicked);
    requestUserData("addInfo", currentUsername); // Send addInfo request with currentUsername
}

void statusForm::on_btnUpdate_clicked() {
    disconnect(ui->btnUpdate, &QPushButton::clicked, this, &statusForm::on_btnUpdate_clicked);
    requestUserData("updateInfo", currentUsername); // Send updateInfo request with currentUsername
}

// Slot for handling the exit button click
void statusForm::on_btnExit_clicked() {
    disconnect(ui->btnExit, &QPushButton::clicked, this, &statusForm::on_btnExit_clicked);
    sendExitRequest();
    QApplication::quit();
}
void statusForm::sendExitRequest() {
    static bool exitRequested = false; // Add a static flag

    if (!exitRequested) {
        requestUserData("Exit");
        exitRequested = true; // Set the flag to true after the request
    }
}

void statusForm::closeEvent(QCloseEvent *event) {
    sendExitRequest();
    event->accept(); // Accept the event to close the window
}


// Slot for handling the points button click
void statusForm::on_btnPoints_clicked() {
    disconnect(ui->btnPoints, &QPushButton::clicked, this, &statusForm::on_btnPoints_clicked);
    requestUserData("showPoints", currentUsername); // Send showPoints request with currentUsername
    QTimer *showPointsTimer = new QTimer(this);
    connect(showPointsTimer, &QTimer::timeout, this, [this]() { requestUserData("showPoints", currentUsername); });
    showPointsTimer->start(1000);
}



// Destructor for statusForm
statusForm::~statusForm()
{
    // No need to delete ui, unique_ptr will handle it
    if (camera) {
        delete camera;
    }
    if (imageCapture) {
        delete imageCapture;
    }
    if (cameraDialog) {
        delete cameraDialog;
    }
    delete buffer; // Delete buffer
    delete mediaPlayer; // Delete mediaPlayer
    delete videoWidget; // Delete videoWidget
}
