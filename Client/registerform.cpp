#include "registerform.h"
#include "ui_registerform.h"
#include "mainwindow.h"
#include "QTcpSocket"
#include "QMessageBox"
#include "QJsonObject"
#include "QJsonDocument"


registerform::registerform(QWidget *parent, MainWindow *mainWindow)
    : QDialog(parent)
    , ui(new Ui::registerform)
    , mainWindow(mainWindow)
    , socket(new QTcpSocket(this))
{
    ui->setupUi(this);
    socket->connectToHost(QHostAddress("127.0.0.1"), 1234);
    if (!socket->waitForConnected(5000)) {
        qDebug() << "statusForm::statusForm: Failed to connect to server.";
    } else {
        qDebug() << "statusForm::statusForm: Connected to server.";
    }
    connect(ui->btnRegisterSubmit, &QPushButton::clicked, this, &registerform::on_btnSubmit_clicked);
    connect(socket.get(), &QTcpSocket::connected, this, &registerform::onConnected);
    connect(socket.get(), &QTcpSocket::errorOccurred, this, &registerform::onSocketError);
    connect(socket.get(), &QTcpSocket::readyRead, this, &registerform::handleServerResponse);

}

registerform::~registerform()
{
    delete ui;
}
void registerform::onConnected() {
    qDebug() << "Connected to server!";
}
void registerform::onSocketError(QTcpSocket::SocketError socketError) {
    qDebug() << "Socket error:" << socket->errorString();
}

void registerform::on_btnSubmit_clicked() {
    disconnect(ui->btnRegisterSubmit, &QPushButton::clicked, this, &registerform::on_btnSubmit_clicked);

    while (true) { // Use a loop for retry mechanism
        QString username = ui->leRegisterUsername->text();
        QString password = ui->leRegisterPassword->text();
        QString rePassword = ui->leRegisterRePassword->text();

        if (username.isEmpty() || password.isEmpty() || rePassword.isEmpty()) {
            QMessageBox::warning(this, "Input Error", "All fields are required.");
            connect(ui->btnRegisterSubmit, &QPushButton::clicked, this, &registerform::on_btnSubmit_clicked); // Reconnect the button
            return; // Exit the function to allow user to correct the input
        }

        if (password != rePassword) {
            QMessageBox::warning(this, "Input Error", "Passwords do not match.");
            connect(ui->btnRegisterSubmit, &QPushButton::clicked, this, &registerform::on_btnSubmit_clicked); // Reconnect the button
            return; // Exit the function to allow user to correct the input
        }

        QJsonObject Data;
        Data["username"] = username;
        Data["password"] = password;    
        // Prepare JSON data
        QJsonObject json;
        json["request"] = "Register";
        json["Data"]=Data;


        QJsonDocument doc(json);
        QByteArray jsonData = doc.toJson();

        // Send JSON data to the server
        socket->write(jsonData);
        break; // Break the loop on successful data submission
    }
}

void registerform::handleServerResponse() {
    QByteArray responseData = socket->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    QJsonObject response = doc.object();
    QString status = response["response"].toString();
    if (status == "userExist") {
        QMessageBox::warning(this, "Registration Error", "User already exists, please use a different username.");
        connect(ui->btnRegisterSubmit, &QPushButton::clicked, this, &registerform::on_btnSubmit_clicked); // Reconnect the button
    } else if (status == "newUser") {
        QMessageBox::information(this, "Registration Success", "You have successfully created an account.");
        this->close();
        mainWindow->show();
    }
}
