#include "server.h"
#include "ui_server.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardItemModel>
#include <QJsonArray>
#include <QDateTime>
#include <QStandardPaths>
#include <QDebug>
#include <limits>
#include <QLoggingCategory>
#include <QDir>
#include <QTimer>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QMessageBox>
#include <QCryptographicHash>
#include <QNetworkInterface>
#include <QDataStream>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QSaveFile>
#include <QImageReader>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

Q_LOGGING_CATEGORY(serverCategory, "server")
Q_LOGGING_CATEGORY(serverLog, "server.log")

server::server(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::server),
    tcpServer(nullptr),
    httpServer(nullptr)
{
    try {
        qInfo() << "Initializing server UI...";
        ui->setupUi(this);
        
        qInfo() << "Creating TCP server...";
        tcpServer = new QTcpServer(this);
        
        qInfo() << "Creating HTTP server...";
        httpServer = new QHttpServer(this);
        
        qInfo() << "Setting up directories...";
        QDir appDir(QCoreApplication::applicationDirPath());
        QStringList requiredDirs = {"account", "status", "timesheet", "data", "avatar", "intro", "points"};
        
        for (const QString &dir : requiredDirs) {
            if (!appDir.exists(dir)) {
                qInfo() << "Creating directory:" << dir;
                if (!appDir.mkpath(dir)) {
                    QString error = QString("Failed to create directory: %1").arg(dir);
                    qCritical() << error;
                    emit serverError(error);
                    return;
                }
            }
        }
        
        qInfo() << "Creating initial JSON files...";
        createInitialJsonFiles();
        
        qInfo() << "Setting up HTTP server...";
        setupHttpServer();
        
        qInfo() << "Setting up TCP server...";
        connect(tcpServer, &QTcpServer::newConnection, this, &server::onNewConnection);
        if (!tcpServer->listen(QHostAddress::Any, 1235)) {
            QString error = QString("TCP Server failed to start: %1").arg(tcpServer->errorString());
            qCritical() << error;
            emit serverError(error);
            return;
        }
        
        qInfo() << "Setting up timers...";
        QTimer *clockTimer = new QTimer(this);
        connect(clockTimer, &QTimer::timeout, this, &server::updateClock);
        clockTimer->start(1000);
        
        QTimer *timesheetTimer = new QTimer(this);
        connect(timesheetTimer, &QTimer::timeout, this, &server::showTimesheet);
        timesheetTimer->start(1000);
        
        qInfo() << "Connecting UI signals...";
        connect(ui->btnCreate, &QPushButton::clicked, this, &server::on_btnCreate_clicked);
        connect(ui->btnDrop, &QPushButton::clicked, this, &server::on_btnDrop_clicked);
        connect(ui->btnChange, &QPushButton::clicked, this, &server::on_btnChange_clicked);
        
        qInfo() << "Server initialization completed successfully";
        
    } catch (const std::exception& e) {
        QString error = QString("Exception during server initialization: %1").arg(e.what());
        qCritical() << error;
        emit serverError(error);
    } catch (...) {
        QString error = "Unknown exception during server initialization";
        qCritical() << error;
        emit serverError(error);
    }
}

server::~server()
{
    delete ui;
    delete tcpServer;
}

void server::setupHttpServer() {
    qInfo() << "Setting up HTTP server...";
    
    // Add health check endpoint first
    httpServer->route("/health", QHttpServerRequest::Method::Get, [] {
        return QHttpServerResponse(QByteArray("OK"), "text/plain", QHttpServerResponse::StatusCode::Ok);
    });
    
    // Thêm error handler
    httpServer->afterRequest([](QHttpServerResponse &&resp) {
        qDebug() << "Request completed with status:" << resp.statusCode();
        return std::move(resp);
    });

    // Try port 8080 instead of 1234
    const auto port = httpServer->listen(QHostAddress::Any, 8080);
    if (!port) {
        qCritical() << "Failed to start HTTP server on port 8080";
        // Try an alternative port
        const auto altPort = httpServer->listen(QHostAddress::Any, 0); // Let the OS choose a port
        if (!altPort) {
            qCritical() << "Failed to start HTTP server on any port";
            emit serverError("Failed to start HTTP server");
            return;
        } else {
            qInfo() << "HTTP server started on alternative port" << altPort;
        }
    } else {
        qInfo() << "HTTP server started successfully on port" << port;
    }

    // Log all network interfaces
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol 
            && address != QHostAddress::LocalHost) {
            qInfo() << "Server accessible at:" 
                   << QString("http://%1:%2").arg(address.toString()).arg(port);
        }
    }
    
    // Add routes for image and video uploads
    httpServer->route("/upload/image", QHttpServerRequest::Method::Post,
                     [this](const QHttpServerRequest &request) {
        qInfo() << "Received image upload request";
        handleImageUpload(request);
        return QHttpServerResponse::StatusCode::Ok;
    });
    
    httpServer->route("/upload/video", QHttpServerRequest::Method::Post,
                     [this](const QHttpServerRequest &request) {
        qInfo() << "Received video upload request";
        handleVideoUpload(request);
        return QHttpServerResponse::StatusCode::Ok;
    });

    qInfo() << "HTTP routes configured successfully";
}

void server::handleImageUpload(const QHttpServerRequest &request) {
    QByteArray rawData = request.body();
    
    // Create directories if they don't exist
    QString baseDir = QCoreApplication::applicationDirPath();
    QString avatarDir = baseDir + "/avatar";
    QDir dir;
    if (!dir.exists(avatarDir)) {
        if (!dir.mkpath(avatarDir)) {
            qCWarning(serverCategory) << "handleImageUpload: Couldn't create avatar directory:" << avatarDir;
            return;
        }
    }
    
    // Find the start and end positions of the base64 data in rawData
    int startIndex = rawData.indexOf("\r\n\r\n") + 4;
    int endIndex = rawData.indexOf("\r\n--", startIndex);
    
    if (startIndex <= 4 || endIndex <= startIndex) {
        qCWarning(serverCategory) << "handleImageUpload: Invalid data format";
        return;
    }
    
    QString base64Data = rawData.mid(startIndex, endIndex - startIndex);

    // Decode the base64 data into binary data
    QByteArray imageData = QByteArray::fromBase64(base64Data.toUtf8());

    // Find the start index of the filename in the header
    int filenameIndex = rawData.indexOf("filename=\"") + 10; // 10 is the length of "filename=\""
    QString username = "default"; // Default username if extraction fails
    
    if (filenameIndex > 10) { // Check if "filename=\"" was found
        int filenameEndIndex = rawData.indexOf("\"", filenameIndex);
        if (filenameEndIndex > filenameIndex) {
            QString filePath = rawData.mid(filenameIndex, filenameEndIndex - filenameIndex);
            qDebug() << "File path:" << filePath;

            // Extract username from the file path
            int lastSlashIndex = filePath.lastIndexOf('/');
            if (lastSlashIndex != -1) {
                username = filePath.mid(lastSlashIndex + 1); // Get the substring after the last slash
                if (username.endsWith(".jpg", Qt::CaseInsensitive)) {
                    username.chop(4); // Remove the last 4 characters (".jpg")
                }
                qDebug() << "Username:" << username;
            }
        }
    }

    // Create a file to save the image
    QString imagePath = avatarDir + "/" + username + ".jpg"; // Path and filename to save the image
    QFile file(imagePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(imageData);
        file.close();
        qDebug() << "Image saved successfully to" << imagePath;
    } else {
        qDebug() << "Failed to save image to" << imagePath;
    }
}

void server::handleVideoUpload(const QHttpServerRequest &request) {
    QByteArray rawVideo = request.body();
    
    // Create directories if they don't exist
    QString baseDir = QCoreApplication::applicationDirPath();
    QString introDir = baseDir + "/intro";
    QDir dir;
    if (!dir.exists(introDir)) {
        if (!dir.mkpath(introDir)) {
            qCWarning(serverCategory) << "handleVideoUpload: Couldn't create intro directory:" << introDir;
            return;
        }
    }
    
    // Assuming the video data starts after the last occurrence of "\r\n\r\n"
    int startIndex = rawVideo.lastIndexOf("\r\n\r\n") + 4;
    int endIndex = rawVideo.indexOf("\r\n--", startIndex);
    
    if (startIndex <= 4 || endIndex <= startIndex) {
        qCWarning(serverCategory) << "handleVideoUpload: Invalid data format";
        return;
    }
    
    QByteArray videoData = rawVideo.mid(startIndex, endIndex - startIndex);
    
    int filenameIndex = rawVideo.indexOf("filename=\"") + 10; // 10 is the length of "filename=\""
    QString username = "default"; // Default username if extraction fails
    
    if (filenameIndex > 10) { // Check if "filename=\"" was found
        int filenameEndIndex = rawVideo.indexOf("\"", filenameIndex);
        if (filenameEndIndex > filenameIndex) {
            QString filePath = rawVideo.mid(filenameIndex, filenameEndIndex - filenameIndex);
            qDebug() << "File path:" << filePath;

            // Extract username from the file path
            int lastSlashIndex = filePath.lastIndexOf('/');
            if (lastSlashIndex != -1) {
                username = filePath.mid(lastSlashIndex + 1); // Get the substring after the last slash
                if (username.endsWith(".mp4", Qt::CaseInsensitive)) {
                    username.chop(4); // Remove the last 4 characters (".mp4")
                }
                qDebug() << "Username:" << username;
            }
        }
    }
    
    // Create a file to save the video
    QString videoPath = introDir + "/" + username + ".mp4"; // Path and filename to save the video
    QFile file(videoPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(videoData);
        file.close();
        qDebug() << "Video saved successfully to" << videoPath;
    } else {
        qDebug() << "Failed to save video to" << videoPath;
    }
}

void server::onNewConnection()
{
    while (tcpServer->hasPendingConnections()) {
        QTcpSocket* socket = tcpServer->nextPendingConnection();
        if (!socket) {
            qCWarning(serverCategory) << "onNewConnection: Failed to get next pending connection.";
            continue;
        }

        // Connect the readyRead signal to a lambda function to handle client data
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            // Ensure there is enough data to determine the request type
            if (socket->bytesAvailable() < 5) {
                return; // Wait for more data
            }

            // Directly handle all data as JSON request
            handleClientData(socket);
        });

        // Connect the disconnected signal to delete the socket later
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}


QJsonObject server::loadJsonFile()
{
    QString relativePath = QCoreApplication::applicationDirPath() + "/account/account.json";

    QFile file(relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "loadJsonData: Couldn't open the file:" << file.errorString();
        return QJsonObject();
    }

    QByteArray fileData = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(fileData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(serverCategory) << "loadJsonData: JSON parse error:" << parseError.errorString();
        return QJsonObject();
    }

    QJsonObject jsonObj = doc.object();

    return jsonObj;
}

void server::handleClientData(QTcpSocket* socket) {
    if (!socket) {
        qCWarning(serverCategory) << "handleClientData: Socket is null.";
        return;
    }

    if (!socket->isOpen()) {
        qCWarning(serverCategory) << "handleClientData: Socket is not open.";
        return;
    }

    // Read and log the raw data from the socket
    QByteArray data = socket->readAll();
    qCDebug(serverCategory) << "handleClientData: Received raw data:" << data;

    // Trim the data and log it
    data = data.trimmed();
    qCDebug(serverCategory) << "handleClientData: Trimmed data:" << data;

    // Validate the JSON data before parsing
    if (!data.startsWith('{') || !data.endsWith('}')) {
        qCWarning(serverCategory) << "handleClientData: Invalid JSON format (missing curly braces):" << data;
        if (socket->isOpen()) {
            QJsonObject responseObj;
            responseObj["response"] = "Invalid JSON format (missing curly braces)";
            QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
            if (!responseData.isEmpty()) {
                socket->write(responseData);
            }
        }
        return;
    }

    // Parse the JSON data
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(serverCategory) << "handleClientData: JSON parse error:" << parseError.errorString();
        qCWarning(serverCategory) << "handleClientData: Received data:" << data;
        if (socket->isOpen()) {
            QJsonObject responseObj;
            responseObj["response"] = "Invalid JSON format";
            responseObj["error"] = parseError.errorString();
            QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
            if (!responseData.isEmpty()) {
                socket->write(responseData);
            }
        }
        return;
    }

    // Log the parsed JSON object
    const QJsonObject obj = doc.object();
    qCDebug(serverCategory) << "handleClientData: Parsed JSON object:" << QJsonDocument(obj).toJson(QJsonDocument::Indented);

    // Handle different types of client requests
    if (obj.contains("request") && obj.value("request").toString() == "loginRequest") {
        const QJsonObject loginData = obj.value("data").toObject();
        const QString username = loginData.value("username").toString();
        const QString password = loginData.value("password").toString();

        if (checkCredentials(username, password)) {
            if (socket->isOpen()) {
                QJsonObject responseObj;
                responseObj["response"] = "Login successful";
                socket->write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact));
            }
            const QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
            saveUserStatus(username, "online", time);
        } else {
            if (socket->isOpen()) {
                QJsonObject responseObj;
                responseObj["response"] = "Incorrect username or password";
                QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
                if (!responseData.isEmpty()) {
                    socket->write(responseData);
                }
            }
        }
    } else if (obj.contains("request") && obj.value("request").toString() == "currentLoginUser"){
        getUserWithClosestTime(socket);
    } else if (obj.contains("request") && obj.value("request").toString() == "Exit") {
        const QString time = QDateTime::currentDateTime().toString("hh:mm:ss");

        qCDebug(serverCategory) << "handleClientData: User" << currentUsername << "requested exit at" << time;

        saveUserStatus(currentUsername, "offline", time);
        if (socket->isOpen()) {
            QJsonObject responseObj;
            responseObj["response"] = "Exit successful";
            QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
            if (!responseData.isEmpty()) {
                socket->write(responseData);
            }
        }
    } else if (obj.contains("request") && obj.value("request").toString() == "showInfo") {
        const QString username = obj.value("username").toString();
        QJsonObject loadedData = loadJsonFile();
        QJsonArray usersArray = loadedData.value("users").toArray();

        bool infoFound = false;
        QJsonObject userInfo;

        for (const QJsonValue &value : usersArray) {
            QJsonObject userObj = value.toObject();
            if (userObj.value("username").toString() == username) {
                if (userObj.contains("info")) {
                    userInfo = userObj.value("info").toObject();
                    infoFound = true;
                }
                break;
            }
        }

        QJsonObject responseObj;
        if (infoFound) {
            responseObj["response"] = "responseInfo";
            responseObj["info"] = userInfo;
        } else {
            responseObj["response"] = "infoEmpty";
        }

        if (socket->isOpen()) {
            QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
            qCDebug(serverCategory) << "Sending response:" << responseData;
            qint64 bytesWritten = socket->write(responseData);
            if (bytesWritten == -1) {
                qCWarning(serverCategory) << "Failed to write to socket:" << socket->errorString();
            } else {
                qCDebug(serverCategory) << "Bytes written to socket:" << bytesWritten;
            }
            socket->flush();
        } else {
            qCWarning(serverCategory) << "Socket is not open.";
        }
    } else if (obj.contains("request") && obj.value("request").toString() == "addInfo") {
        const QString username = obj.value("username").toString();
        QJsonObject loadedData = loadJsonFile();
        QJsonArray usersArray = loadedData.value("users").toArray();

        bool infoFound = false;
        for (const QJsonValue &value : usersArray) {
            QJsonObject userObj = value.toObject();
            if (userObj.value("username").toString() == username && userObj.contains("info")) {
                infoFound = true;
                break;
            }
        }

        if (socket->isOpen()) {
            QJsonObject responseObj;
            if (infoFound) {
                responseObj["response"] = "infoAvailable";
            } else {
                responseObj["response"] = "addInfo";
            }
            QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
            if (!responseData.isEmpty()) {
                socket->write(responseData);
            }
        }
    } else if (obj.contains("request") && obj.value("request").toString() == "saveInfo") {
        const QString username = obj.value("username").toString();
        const QJsonObject infoData = obj.value("info").toObject();
        saveInfoData(username, infoData, socket);
    } else if (obj.contains("request") && obj.value("request").toString() == "updateInfo") {
        const QString username = obj.value("username").toString();
        QJsonObject loadedData = loadJsonFile();
        QJsonArray usersArray = loadedData.value("users").toArray();

        bool infoFound = false;
        for (const QJsonValue &value : usersArray) {
            QJsonObject userObj = value.toObject();
            if (userObj.value("username").toString() == username && userObj.contains("info")) {
                infoFound = true;
                break;
            }
        }

        if (socket->isOpen()) {
            QJsonObject responseObj;
            if (infoFound) {
                responseObj["response"] = "updateInfo";
            } else {
                responseObj["response"] = "noInfo";
            }
            QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
            if (!responseData.isEmpty()) {
                socket->write(responseData);
            }
        }
    } else if (obj.contains("request") && obj.value("request").toString() == "showPoints") {
        const QString username = obj.value("username").toString();
        showCurrentPoints(username, socket);
    } else if (obj.contains("request") && obj.value("request").toString() == "Register") {
        const QJsonObject requestData = obj.value("Data").toObject(); // Corrected from "Data" to obj.value("Data").toObject()
        handleClientRegister(socket, requestData);
    } else {
        qCWarning(serverCategory) << "handleClientData: Missing required fields in JSON object:" << QJsonDocument(obj).toJson(QJsonDocument::Indented);
        if (socket->isOpen()) {
            QJsonObject responseObj;
            responseObj["response"] = "Invalid Client's data format (missing required fields)";
            socket->write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact));
        }
    }
}

void server::handleClientRegister(QTcpSocket* socket, const QJsonObject &requestObj) {
    QString username = requestObj.value("username").toString();
    QString password = requestObj.value("password").toString(); // Correctly extracting password

    QJsonObject loadedData = loadJsonFile();
    QJsonArray usersArray = loadedData.value("users").toArray();

    // Check if username already exists
    bool userExists = false;
    for (const QJsonValue &value : usersArray) {
        QJsonObject userObj = value.toObject();
        if (userObj.value("username").toString() == username) {
            userExists = true;
            break;
        }
    }

    if (userExists) {
        QJsonObject responseObj;
        responseObj["response"] = "userExist";
        socket->write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact));
        return;
    }

    // Create new user object
    QJsonObject newUser;
    newUser["username"] = username;
    newUser["password"] = password; // Store password, consider hashing in a real application

    // Add new user to array and save
    usersArray.append(newUser);
    loadedData["users"] = usersArray;
    saveJsonFile(loadedData); // Assuming you have a function to save the JSON file

    // Send response back to client
    QJsonObject responseObj;
    responseObj["response"] = "newUser";
    socket->write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact));
}

// Assuming you have a function to save the JSON file
void server::saveJsonFile(const QJsonObject &data) {
    QString filePath = QCoreApplication::applicationDirPath() + "/account/account.json";
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(serverCategory) << "Failed to open file for writing:" << file.errorString();
        return;
    }
    file.write(QJsonDocument(data).toJson(QJsonDocument::Indented));
    file.close();
    QMessageBox::information(this, "Infomation", "Saved successfull");
}

bool server::checkCredentials(const QString &username, const QString &password) {
    QJsonObject loadedData = loadJsonFile();

    if (loadedData.isEmpty()) {
        qCWarning(serverCategory) << "checkCredentials: Failed to load or parse JSON data.";
        return false;
    }

    qCDebug(serverCategory) << "checkCredentials: Loaded JSON data:" << QJsonDocument(loadedData).toJson(QJsonDocument::Indented);

    const QJsonArray accountsArray = loadedData.value("users").toArray();
    for (const QJsonValue &value : accountsArray) {
        const QJsonObject accountObj = value.toObject();
        if (accountObj.value("username").toString() == username && accountObj.value("password").toString() == password) {
            currentUsername = username;
            return true;
        }
    }

    qCWarning(serverCategory) << "checkCredentials: Username or password incorrect.";
    return false;
}

QJsonArray server::handleUserStatusRequest() {
    QJsonObject jsonData = loadJsonFile();
    QJsonArray accountsArray = jsonData["users"].toArray();

    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString baseDir = QCoreApplication::applicationDirPath();
    QString dirPath = baseDir + "/status/" + date;
    QString filePath = dirPath + "/status.json";

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "handleUserStatusRequest: Couldn't open the file:" << file.errorString();
        return QJsonArray();
    }

    QByteArray fileData = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(fileData);
    QJsonObject rootObj = doc.object();
    QJsonArray users = rootObj.value("users").toArray();

    QJsonArray responseArray;
    for (const QJsonValue &accountValue : accountsArray) {
        QJsonObject accountObj = accountValue.toObject();
        QString username = accountObj["username"].toString();

        QJsonObject closestUserObj;
        qint64 closestTimeDiff = std::numeric_limits<qint64>::max();
        QDateTime currentTime = QDateTime::currentDateTime();
        QString currentTimeStr = currentTime.toString("hh:mm:ss");
        currentTime = QDateTime::fromString(currentTimeStr, "hh:mm:ss");
        for (const QJsonValue &userValue : users) {
            QJsonObject userObj = userValue.toObject();
            if (userObj["username"].toString() == username) {
                QString userTimeStr = userObj["time"].toString("hh:mm:ss");
                QDateTime userTime = QDateTime::fromString(userTimeStr, "hh:mm:ss");
                if (userTime < currentTime) {
                    qint64 timeDiff = qAbs(userTime.msecsTo(currentTime));
                    if (timeDiff < closestTimeDiff) {
                        closestTimeDiff = timeDiff;
                        closestUserObj = userObj;
                    }
                }
            }
        }

        if (!closestUserObj.isEmpty()) {
            responseArray.append(closestUserObj);
        }
    }

    return responseArray;
}

void server::getUserWithClosestTime(QTcpSocket* socket) {
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString baseDir = QCoreApplication::applicationDirPath();
    QString dirPath = baseDir + "/status/" + date;
    QString filePath = dirPath + "/status.json";

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "getUserWithClosestTime: Couldn't open the file:" << file.errorString();
        if (socket->isOpen()) {
            QJsonObject responseObj;
            responseObj["response"] = "Error: Couldn't open the status file.";
            socket->write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact));
        }
        return;
    }

    QByteArray fileData = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(fileData);
    QJsonObject rootObj = doc.object();
    QJsonArray users = rootObj.value("users").toArray();

    QJsonObject closestUserObj;
    qint64 closestTimeDiff = std::numeric_limits<qint64>::max();
    QDateTime currentTime = QDateTime::currentDateTime();

    for (const QJsonValue &userValue : users) {
        QJsonObject userObj = userValue.toObject();
        QString userTimeStr = userObj["time"].toString();
        QDateTime userTime = QDateTime::fromString(userTimeStr, "hh:mm:ss");
        if (userTime.isValid()) {
            qint64 timeDiff = qAbs(currentTime.msecsTo(userTime));
            if (timeDiff < closestTimeDiff) {
                closestTimeDiff = timeDiff;
                closestUserObj = userObj;
            }
        }
    }

    QJsonObject responseObj;
    responseObj["response"] = "currentLoginUser";
    QJsonArray usersArray;
    usersArray.append(closestUserObj);
    responseObj["users"] = usersArray;
    QByteArray responseMessage = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);

    socket->write(responseMessage);
    if (!socket->waitForBytesWritten()) {
        qCWarning(serverCategory) << "getUserWithClosestTime: Failed to send user status.";
    }
}

void server::on_btnView_clicked() {
    QJsonArray responseArray = handleUserStatusRequest();

    QStandardItemModel *model = new QStandardItemModel(responseArray.size(), 3, this);
    model->setHorizontalHeaderLabels({"Username", "Time", "Status"});

    for (int row = 0; row < responseArray.size(); ++row) {
        QJsonObject userObj = responseArray[row].toObject();
        model->setItem(row, 0, new QStandardItem(userObj["username"].toString()));
        model->setItem(row, 1, new QStandardItem(userObj["time"].toString()));

        QStandardItem *statusItem = new QStandardItem();
        QString status = userObj["status"].toString();
        if (status == "online") {
            statusItem->setIcon(QIcon(":/icon/shape.png"));
        } else if (status == "offline") {
            statusItem->setIcon(QIcon(":/icon/circle.png"));
        }
        model->setItem(row, 2, statusItem);
    }

    ui->tableView->setModel(model);
    QTimer *viewTimer = new QTimer(this);
    connect(viewTimer, &QTimer::timeout, this, &server::on_btnView_clicked);
    viewTimer->start(10000);
}

void server::saveUserStatus(const QString &username, const QString &status, const QString &time) {
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString baseDir = QCoreApplication::applicationDirPath();
    QString dirPath = baseDir + "/status/" + date;
    qCDebug(serverCategory) << "saveUserStatus: Directory path:" << dirPath;

    QDir dir;
    if (!dir.exists(dirPath)) {
        if (!dir.mkpath(dirPath)) {
            qCWarning(serverCategory) << "saveUserStatus: Couldn't create the directory:" << dirPath;
            return;
        }
    }

    QString filePath = dirPath + "/status.json";
    qCDebug(serverCategory) << "saveUserStatus: File path:" << filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        qCWarning(serverCategory) << "saveUserStatus: Couldn't open the file:" << file.errorString();
        return;
    }

    QJsonDocument doc;
    QJsonObject rootObj;
    QJsonArray users;

    if (file.size() > 0) {
        const QByteArray jsonData = file.readAll();
        QJsonParseError parseError;
        doc = QJsonDocument::fromJson(jsonData, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(serverCategory) << "saveUserStatus: JSON parse error:" << parseError.errorString();
            file.close();
            return;
        }
        rootObj = doc.object();
        users = rootObj.value("users").toArray();
    }

    QJsonObject userObj;
    userObj["username"] = username;
    userObj["status"] = status;
    userObj["time"] = time;
    users.append(userObj);

    rootObj["users"] = users;
    doc.setObject(rootObj);
    file.resize(0);
    if (file.write(doc.toJson(QJsonDocument::Indented)) == -1) {
        qCWarning(serverCategory) << "saveUserStatus: Failed to write to the file:" << file.errorString();
    } else {
        qCDebug(serverCategory) << "saveUserStatus: User status recorded:" << username << status << time;
    }

    file.close();
}

void server::on_btnSubmit_clicked()
{
    QString date = ui->dateEdit->date().toString("yyyy-MM-dd");
    QString baseDir = QCoreApplication::applicationDirPath();
    QString statusFilePath = baseDir + "/status/" + date + "/status.json";
    QString pointsFilePath = baseDir + "/points/" + date + "_points.json";

    // Handle status file
    QFile statusFile(statusFilePath);
    if (!statusFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "on_btnSubmit_clicked: Couldn't open the status file:" << statusFile.errorString();
        return;
    }

    QByteArray statusData = statusFile.readAll();
    QJsonDocument statusDoc = QJsonDocument::fromJson(statusData);
    QJsonObject statusRootObj = statusDoc.object();
    QJsonArray users = statusRootObj.value("users").toArray();
    statusFile.close();

    QJsonArray responseArray;
    QDateTime requestTime = QDateTime::fromString(ui->timeEdit->time().toString("hh:mm:ss"), "hh:mm:ss");

    QMap<QString, QJsonObject> closestUsers;

    for (const QJsonValue &userValue : users) {
        QJsonObject userObj = userValue.toObject();
        QString username = userObj["username"].toString();
        QDateTime userTime = QDateTime::fromString(userObj["time"].toString(), "hh:mm:ss");

        if (userTime <= requestTime) {
            if (!closestUsers.contains(username) ||
                QDateTime::fromString(closestUsers[username]["time"].toString(), "hh:mm:ss") < userTime) {
                closestUsers[username] = userObj;
            }
        }
    }

    for (const QJsonObject &userObj : closestUsers) {
        responseArray.append(userObj);
    }

    QStandardItemModel *statusModel = new QStandardItemModel(responseArray.size(), 3, this);
    statusModel->setHorizontalHeaderLabels({"Username", "Time", "Status"});

    for (int row = 0; row < responseArray.size(); ++row) {
        QJsonObject userObj = responseArray[row].toObject();
        statusModel->setItem(row, 0, new QStandardItem(userObj["username"].toString()));
        statusModel->setItem(row, 1, new QStandardItem(userObj["time"].toString()));

        QStandardItem *statusItem = new QStandardItem();
        QString status = userObj["status"].toString();
        if (status == "online") {
            statusItem->setIcon(QIcon(":/icon/icon/shape.png"));
        } else if (status == "offline") {
            statusItem->setIcon(QIcon(":/icon/icon/circle.png"));
        }
        statusModel->setItem(row, 2, statusItem);
    }

    ui->tableView->setModel(statusModel);

    // Handle points file
    QFile pointsFile(pointsFilePath);
    if (!pointsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "on_btnSubmit_clicked: Couldn't open the points file:" << pointsFile.errorString();
        return;
    }

    QByteArray pointsData = pointsFile.readAll();
    QJsonDocument pointsDoc = QJsonDocument::fromJson(pointsData);
    QJsonObject pointsObj = pointsDoc.object();
    pointsFile.close();

    QStandardItemModel *pointsModel = new QStandardItemModel(pointsObj.size(), 2, this);
    pointsModel->setHorizontalHeaderLabels({"Username", "Points"});

    int row = 0;
    for (auto it = pointsObj.begin(); it != pointsObj.end(); ++it, ++row) {
        pointsModel->setItem(row, 0, new QStandardItem(it.key()));
        pointsModel->setItem(row, 1, new QStandardItem(QString::number(it.value().toInt())));
    }

    ui->tableTimesheet->setModel(pointsModel);
}

void server::saveInfoData(const QString &username, const QJsonObject &infoData, QTcpSocket* socket) {
    QJsonObject loadedData = loadJsonFile();
    QJsonArray usersArray = loadedData.value("users").toArray();

    bool userFound = false;
    for (int i = 0; i < usersArray.size(); ++i) {
        QJsonObject userObj = usersArray[i].toObject();
        if (userObj.value("username").toString() == username) {
            userObj["info"] = infoData;
            usersArray[i] = userObj;
            userFound = true;
            break;
        }
    }

    if (!userFound) {
        qCWarning(serverCategory) << "saveInfoData: Username not found in JSON data.";
        if (socket->isOpen()) {
            QJsonObject responseObj;
            responseObj["response"] = "Username not found";
            QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
            if (!responseData.isEmpty()) {
                socket->write(responseData);
            }
        }
        return;
    }

    loadedData["users"] = usersArray;

    QString relativePath = QCoreApplication::applicationDirPath() + "/account/account.json";
    QFile file(relativePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "saveInfoData: Couldn't open the file for writing:" << file.errorString();
        if (socket->isOpen()) {
            QJsonObject responseObj;
            responseObj["response"] = "Failed to save data";
            QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
            if (!responseData.isEmpty()) {
                socket->write(responseData);
            }
        }
        return;
    }

    file.write(QJsonDocument(loadedData).toJson(QJsonDocument::Indented));
    file.close();

    if (socket->isOpen()) {
        QJsonObject responseObj;
        responseObj["response"] = "Info saved successfully";
        QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
        if (!responseData.isEmpty()) {
            socket->write(responseData);
        }
    }
}


void server::updateClock()
{
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    ui->lblClock->setText(currentTime);
}


void server::showTimesheet() {
    QJsonObject jsonData = loadJsonFile();
    QJsonArray accountsArray = jsonData["users"].toArray();

    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString baseDir = QCoreApplication::applicationDirPath();
    QString dirPath = baseDir + "/status/" + date;
    QString filePath = dirPath + "/status.json";

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "showTimesheet: Couldn't open the file:" << file.errorString();
        return;
    }

    QByteArray fileData = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(fileData);
    QJsonObject rootObj = doc.object();
    QJsonArray users = rootObj.value("users").toArray();

    QStandardItemModel *model = new QStandardItemModel(accountsArray.size(), 6, this);
    model->setHorizontalHeaderLabels({"Username", "Start", "End", "Bonus", "Minus", "Points"});

    for (int row = 0; row < accountsArray.size(); ++row) {
        QJsonObject accountObj = accountsArray[row].toObject();
        QString username = accountObj["username"].toString();

        QDateTime startTime, endTime;
        int bonus = 0, minus = 0, points = 0;
        bool isOnline = false;
        bool firstOnlineFound = false;

        for (const QJsonValue &userValue : users) {
            QJsonObject userObj = userValue.toObject();
            if (userObj["username"].toString() == username) {
                QString status = userObj["status"].toString();
                QDateTime time = QDateTime::fromString(userObj["time"].toString(), "hh:mm:ss");

                if (status == "online") {
                    if (!firstOnlineFound) {
                        startTime = time;
                        firstOnlineFound = true;
                    }
                    if (!isOnline) {
                        startTime = time;
                        isOnline = true;
                    }
                } else if (status == "offline" && isOnline) {
                    endTime = time;
                    isOnline = false;

                    if (startTime.isValid() && endTime.isValid()) {
                        qint64 duration = startTime.secsTo(endTime);
                        points += (duration * 5) + bonus - minus;
                    }
                }
            }
        }

        if (isOnline) {
            endTime = QDateTime::fromString(QDateTime::currentDateTime().toString("hh:mm:ss"), "hh:mm:ss");
            if (startTime.isValid() && endTime.isValid()) {
                qint64 duration = startTime.secsTo(endTime);
                points += (duration * 5) + bonus - minus;
            }
        }


        // Ensure startTime and endTime are valid before displaying
        QString startTimeStr = startTime.isValid() ? startTime.toString("hh:mm:ss") : "N/A";
        QString endTimeStr = endTime.isValid() ? endTime.toString("hh:mm:ss") : "N/A";

        model->setItem(row, 0, new QStandardItem(username));
        model->setItem(row, 1, new QStandardItem(startTimeStr));
        model->setItem(row, 2, new QStandardItem(endTimeStr));
        model->setItem(row, 3, new QStandardItem(QString::number(bonus)));
        model->setItem(row, 4, new QStandardItem(QString::number(minus)));
        model->setItem(row, 5, new QStandardItem(QString::number(points)));



    }

    ui->tableTimesheet->setModel(model);

    QJsonObject pointsObj;

    for (int row = 0; row < accountsArray.size(); ++row) {

        QJsonObject accountObj = accountsArray[row].toObject();

        QString username = accountObj["username"].toString();

        pointsObj[username] = model->item(row, 5)->text().toInt();

    }


    QString pointsFilePath = baseDir + "/points/" + date + "_points.json";
    QDir pointsDir(baseDir + "/points");

    if (!pointsDir.exists()) {

        pointsDir.mkpath(".");

    }


    QFile pointsFile(pointsFilePath);

    if (!pointsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {

        qCWarning(serverCategory) << "showTimesheet: Couldn't open the points file for writing:" << pointsFile.errorString();

        return;

    }

    QJsonDocument pointsDoc(pointsObj);


    pointsFile.write(pointsDoc.toJson(QJsonDocument::Indented));

    pointsFile.close();
}

void server::showCurrentPoints(const QString &username, QTcpSocket* socket) {
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString baseDir = QCoreApplication::applicationDirPath();
    QString pointsFilePath = baseDir + "/points/" + date + "_points.json";

    QFile pointsFile(pointsFilePath);
    if (!pointsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "showCurrentPoints: Couldn't open the points file:" << pointsFile.errorString();
        if (socket->isOpen()) {
            QJsonObject responseObj;
            responseObj["response"] = "Error: Couldn't open the points file.";
            socket->write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact));
        }
        return;
    }

    QByteArray pointsData = pointsFile.readAll();
    QJsonDocument pointsDoc = QJsonDocument::fromJson(pointsData);
    QJsonObject pointsObj = pointsDoc.object();
    pointsFile.close();

    int points = pointsObj.value(username).toInt();

    QJsonObject responseObj;
    responseObj["response"] = "currentPoints";
    responseObj["username"] = username;
    responseObj["points"] = points;

    if (socket->isOpen()) {
        QByteArray responseData = QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
        socket->write(responseData);
    }
}

void server::on_btnCreate_clicked() {
    disconnect(ui->btnCreate, &QPushButton::clicked, this, &server::on_btnCreate_clicked);
    QString username = ui->leUsername->text(); // Lấy username từ QLineEdit
    QString password = "admin"; // Password mặc định

    // Đọc dữ liệu hiện tại từ file
    QJsonObject loadedData = loadJsonFile();
    QJsonArray usersArray = loadedData.value("users").toArray();

    // Tạo đối tượng người dùng mới
    QJsonObject newUser;
    newUser["username"] = username;
    newUser["password"] = password;

    // Thêm người dùng mới vào mảng
    usersArray.append(newUser);
    loadedData["users"] = usersArray;

    // Ghi lại vào file
    QString relativePath = QCoreApplication::applicationDirPath() + "/account/account.json";
    QFile file(relativePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "on_btnCreate_clicked: Couldn't open the file for writing:" << file.errorString();
        return;
    }

    QJsonDocument doc(loadedData);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}


void server::on_btnDrop_clicked() {
    disconnect(ui->btnDrop, &QPushButton::clicked, this, &server::on_btnDrop_clicked);
    QString username = ui->leUsername->text(); // Lấy username từ QLabel

    // Đọc dữ liệu hiện tại từ file
    QJsonObject loadedData = loadJsonFile();
    QJsonArray usersArray = loadedData.value("users").toArray();

    // Tạo một QJsonArray mới để lưu trữ các người dùng không bị xóa
    QJsonArray updatedUsersArray;

    // Duyệt qua mảng người dùng hiện tại và xóa người dùng có username tương ứng
    for (int i = 0; i < usersArray.size(); ++i) {
        QJsonObject user = usersArray[i].toObject();
        if (user["username"].toString() != username) {
            updatedUsersArray.append(user);
        }
    }

    // Cập nhật mảng người dùng trong dữ liệu JSON
    loadedData["users"] = updatedUsersArray;

    // Ghi lại vào file
    QString relativePath = QCoreApplication::applicationDirPath() + "/account/account.json";
    QFile file(relativePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(serverCategory) << "on_btnDrop_clicked: Couldn't open the file for writing:" << file.errorString();
        return;
    }

    QJsonDocument doc(loadedData);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

void server::on_btnChange_clicked() {
    disconnect(ui->btnChange, &QPushButton::clicked, this, &server::on_btnChange_clicked);
    QString username = ui->leUsernameChange->text(); // Get the username from QLineEdit

    // Read the current data from the file
    QJsonObject loadedData = loadJsonFile();
    QJsonArray usersArray = loadedData.value("users").toArray();
    bool userExists = false;

    // Iterate over the users array to find the user and update their info
    for (int i = 0; i < usersArray.size(); ++i) {
        QJsonObject userObj = usersArray[i].toObject();
        if (userObj["username"].toString() == username) {
            userExists = true;
            QJsonObject infoObj = userObj["info"].toObject(); // Assuming each user has an "info" object

            // Update user information if fields are not empty
            QString fullname = ui->leFullnameChange->text();
            if (!fullname.isEmpty()) infoObj["Fullname"] = fullname;

            QString birthday = ui->leBirthdayChange->text();
            if (!birthday.isEmpty()) infoObj["Birthday"] = birthday;

            QString sex = ui->leSexChange->text();
            if (!sex.isEmpty()) infoObj["Sex"] = sex;

            QString email = ui->leEmailChange->text();
            if (!email.isEmpty()) infoObj["Email"] = email;

            QString tel = ui->leTelChange->text();
            if (!tel.isEmpty()) infoObj["Tel"] = tel;

            userObj["info"] = infoObj; // Update the user object with the modified info object
            usersArray[i] = userObj; // Update the array with the modified user object
            break;
        }
    }

    if (!userExists) {
        QMessageBox::information(this, "Infomation", "Username not exist");
    } else {
        // Write the updated data back to the file
        loadedData["users"] = usersArray;
        saveJsonFile(loadedData); // Assuming you have a function to save the JSON file
    }
}

void server::createInitialJsonFiles() {
    QString baseDir = QCoreApplication::applicationDirPath();
    
    // Create account.json if it doesn't exist
    QString accountPath = baseDir + "/account/account.json";
    QFile accountFile(accountPath);
    if (!accountFile.exists()) {
        if (accountFile.open(QIODevice::WriteOnly)) {
            QJsonObject rootObj;
            QJsonArray usersArray;
            rootObj["users"] = usersArray;
            accountFile.write(QJsonDocument(rootObj).toJson());
            accountFile.close();
            qInfo() << "Created initial account.json file";
        } else {
            qCritical() << "Failed to create account.json file:" << accountFile.errorString();
        }
    }
    
    // Create initial status.json
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString statusDir = baseDir + "/status/" + date;
    QDir dir;
    if (!dir.exists(statusDir)) {
        if (!dir.mkpath(statusDir)) {
            qCritical() << "Failed to create status directory:" << statusDir;
        }
    }
    
    QString statusPath = statusDir + "/status.json";
    QFile statusFile(statusPath);
    if (!statusFile.exists()) {
        if (statusFile.open(QIODevice::WriteOnly)) {
            QJsonObject rootObj;
            QJsonArray usersArray;
            rootObj["users"] = usersArray;
            statusFile.write(QJsonDocument(rootObj).toJson());
            statusFile.close();
            qInfo() << "Created initial status.json file";
        } else {
            qCritical() << "Failed to create status.json file:" << statusFile.errorString();
        }
    }
}

void server::show() {
    QMainWindow::show();  // Call base class show()
    emit serverReady();
}


