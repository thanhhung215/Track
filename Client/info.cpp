#include "info.h"
#include "ui_info.h"
#include "statusform.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QMessageBox>
#include <QHostAddress>
#include <QJsonArray>

Info::Info(QWidget *parent, statusForm *statusF)
    : QDialog(parent)
    , ui(new Ui::Info)
    , statusF(statusF ? statusF : new statusForm(this)) // Ensure statusF is initialized
    , socket(new QTcpSocket(this))
{
    ui->setupUi(this);
    socket->connectToHost(QHostAddress("127.0.0.1"), 1234); // Establish connection in constructor

    // Connect the button click signal to the slot
    connect(ui->btnSubmit, &QPushButton::clicked, this, &Info::on_btnSubmit_clicked);

    // Connect the readyRead signal to the handleResponse slot
    connect(socket, &QTcpSocket::readyRead, this, &Info::handleResponse);

    // Connect destroyed signal to show slot of statusF
    connect(this, &QObject::destroyed, statusF, &statusForm::show);
}

Info::~Info() {
    delete ui;
    // Destructor implementation (if needed)
}

void Info::handleResponse()
{
    if (!socket) {
        qDebug() << "Socket is null";
        return;
    }

    QByteArray responseData = socket->readAll();

    QJsonParseError parseError;
    QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qDebug() << "JSON parse error:" << parseError.errorString();
        return;
    }

    QJsonObject responseObject = responseDoc.object();
    QString responseMessage = responseObject["response"].toString();
    qDebug() << "Response received:" << responseMessage;

    if (responseMessage == "Info saved successfully") {
    QMessageBox::information(this, "Success", "Info saved successfully");
    this->close(); // Hide the statusForm
    statusF->show(); // Show the statusForm
    } else if (responseMessage == "Info save failed") {
        QMessageBox::critical(this, "Error", "Failed to save info");
    } else {
        qDebug() << "Unexpected response:" << responseMessage;
        QMessageBox::warning(this, "Warning", "Unexpected response from server");
    }
}

void Info::on_btnSubmit_clicked()
{
    disconnect(ui->btnSubmit, &QPushButton::clicked, this, &Info::on_btnSubmit_clicked);
    qDebug() << "Button clicked";

    try {
        if (!socket) {
            qDebug() << "Socket is null";
            return;
        }

        if (!statusF) {
            qDebug() << "statusF is null";
            return;
        }

        if (socket->state() == QAbstractSocket::ConnectedState) {
            qDebug() << "Socket is connected";

            if (!ui->leFullname || !ui->leBirthday || !ui->leSex || !ui->leEmail || !ui->leTel) {
                qDebug() << "One or more UI elements are null";
                return;
            }

            QJsonObject json;
            json["request"] = "saveInfo";
            json["username"] = statusF->currentUsername; // Assuming getCurrentUsername() is a method in statusform
            
            QJsonObject info;
            info["Fullname"] = ui->leFullname->text();
            info["Birthday"] = ui->leBirthday->text();
            info["Sex"] = ui->leSex->text();
            info["Email"] = ui->leEmail->text();
            info["Tel"] = ui->leTel->text();

            json["info"] = info;

            QJsonDocument doc(json);
            QByteArray data = doc.toJson(QJsonDocument::Compact); // Ensure compact JSON format


            qint64 bytesWritten = socket->write(data);
            if (bytesWritten == -1) {
                qDebug() << "Failed to write data to socket";
            } else {
                qDebug() << "Data written to socket, bytes:" << bytesWritten;
            }

            socket->flush();
        } else {
            qDebug() << "Socket is not connected";
            QMessageBox::critical(this, "Error", "Not connected to server");
        }
    } catch (const std::exception &e) {
        qDebug() << "Exception caught:" << e.what();
    } catch (...) {
        qDebug() << "Unknown exception caught";
    }
}
