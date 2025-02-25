#include <QHostAddress>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>
#include <QProcess>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "statusform.h"
#include "registerform.h"
#include "ClickableLabel.h"

// Constructor
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , socket(new QTcpSocket(this))
{
    ui->setupUi(this);

    // Connect socket signals to slots
    connect(socket, &QTcpSocket::connected, this, &MainWindow::onConnected);
    connect(socket, &QTcpSocket::disconnected, this, &MainWindow::onDisconnected);
    connect(socket, &QTcpSocket::readyRead, this, &MainWindow::readData);
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &MainWindow::onError);

    // Connect to server
    connectToServer(QHostAddress("127.0.0.1"), 1234);

    // Connect UI signals to slots
    qDebug() << "Connecting login button signal to slot";
    connect(ui->btnLogin, &QPushButton::clicked, this, &MainWindow::on_btnLogin_clicked);

    // Correct the connection setup for ClickableLabel
    connect(ui->lblRegister, &ClickableLabel::clicked, this, &MainWindow::on_lblRegister_clicked);

}

// Destructor
MainWindow::~MainWindow()
{
    if (socket->isOpen()) {
        socket->close();
    }
    delete ui;
}

// Connect to server
void MainWindow::connectToServer(const QHostAddress &host, int port) {
    if (socket->state() == QAbstractSocket::UnconnectedState) {
        socket->connectToHost(host, port);
    } else {
        qDebug() << "Already connected or connecting.";
    }
}

// Slot for successful connection
void MainWindow::onConnected() {
    qDebug() << "onConnected: Connected to server!";
    QMessageBox::information(this, "Connection", "Successfully connected to the server.");
}

// Slot for disconnection
void MainWindow::onDisconnected() {
    qDebug() << "onDisconnected: Disconnected from server!";
    QMessageBox::warning(this, "Disconnection", "Disconnected from the server.");
}

// Slot for handling errors
void MainWindow::onError(QAbstractSocket::SocketError socketError) {
    QString errorMessage;
    switch (socketError) {
    case QAbstractSocket::HostNotFoundError:
        errorMessage = "Host not found.";
        break;
    case QAbstractSocket::ConnectionRefusedError:
        errorMessage = "Connection refused.";
        break;
    default:
        errorMessage = "An unknown error occurred.";
        break;
    }
    qDebug() << "onError: Error:" << socket->errorString() << "Code:" << socketError;
    QMessageBox::critical(this, "Error", errorMessage);
}

// Slot for reading data from server
void MainWindow::readData() {
    QByteArray data = socket->readAll();
    qDebug() << "readData: Received from server:" << data;

    QString response = QString::fromUtf8(data);

    // Handle login response
    handleLoginResponse(response);

    // Notify user based on server response
    if (response.contains("User status saved successfully")) {
        QMessageBox::information(this, "Status Update", "User status saved successfully.");
    } else if (response.contains("Failed to save user status")) {
        QMessageBox::warning(this, "Status Update", "Failed to save user status.");
    }
}

// Handle login response from server
void MainWindow::handleLoginResponse(const QString &response) {
    if (response.contains("Login successful")) {
        qDebug() << "handleLoginResponse: Login successful!";
        QMessageBox::information(this, "Login", "Login successful!");
        // sendStatusRequest(); // Send status update after successful login
        this->hide();
        statusForm *form = new statusForm(this, this); // No parent
        form->show();
    } else if (response.contains("Incorrect username or password")) {
        qDebug() << "handleLoginResponse: Incorrect username or password";
        QMessageBox::warning(this, "Login", "Incorrect username or password");
    }
}


// Slot for login button click
void MainWindow::on_btnLogin_clicked() {
    qDebug() << "on_btnLogin_clicked: Login button clicked";

    // Disconnect the button to prevent multiple clicks
    disconnect(ui->btnLogin, &QPushButton::clicked, this, &MainWindow::on_btnLogin_clicked);

    QString username = ui->leUsername->text();
    QString password = ui->lePassword->text();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "Login", "Username and password cannot be empty.");
        // Reconnect the button if the input is invalid
        connect(ui->btnLogin, &QPushButton::clicked, this, &MainWindow::on_btnLogin_clicked);
        return;
    }

    QJsonObject loginData;
    loginData["username"] = username;
    loginData["password"] = password;
    QJsonObject requestData;
    requestData["request"] = "loginRequest";
    requestData["data"] = loginData; // Add loginData to requestData

    QJsonDocument doc(requestData);
    QByteArray message = doc.toJson(QJsonDocument::Compact);

    qDebug() << "on_btnLogin_clicked: Preparing to send to server:" << message;

    if (socket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "on_btnLogin_clicked: Socket is not connected. Attempting to reconnect.";
        connectToServer(QHostAddress("127.0.0.1"), 1234);
        if (!socket->waitForConnected()) {
            qDebug() << "on_btnLogin_clicked: Failed to reconnect.";
            QMessageBox::critical(this, "Error", "Failed to reconnect to the server.");
            // Reconnect the button if the connection fails
            connect(ui->btnLogin, &QPushButton::clicked, this, &MainWindow::on_btnLogin_clicked);
            return;
        }
    }

    qDebug() << "on_btnLogin_clicked: Sending message to server";
    socket->write(message);
    if (!socket->waitForBytesWritten()) {
        qDebug() << "on_btnLogin_clicked: Failed to send login request.";
        QMessageBox::critical(this, "Error", "Failed to send login request.");
    }

    // Reconnect the button after the request is sent
    connect(ui->btnLogin, &QPushButton::clicked, this, &MainWindow::on_btnLogin_clicked);
}

// Get username from UI
QString MainWindow::getUsername() const {
    return ui->leUsername->text();
}

void MainWindow::on_lblRegister_clicked() {
    disconnect(ui->lblRegister, &ClickableLabel::clicked, this, &MainWindow::on_lblRegister_clicked);
    this->hide();
    registerform *form = new registerform(this, this);
    form->show();
}

