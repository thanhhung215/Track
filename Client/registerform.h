#ifndef REGISTERFORM_H
#define REGISTERFORM_H

#include <QDialog>
#include <QTcpSocket>

class MainWindow;
namespace Ui {
class registerform;
}

class registerform : public QDialog
{
    Q_OBJECT

public:
    explicit registerform(QWidget *parent = nullptr, MainWindow *mainWindow = nullptr);
    ~registerform();

private:
    Ui::registerform *ui;
    MainWindow *mainWindow;
    std::unique_ptr<QTcpSocket> socket;
    void on_btnSubmit_clicked();
    void handleServerResponse();
    void onConnected();
    void onSocketError(QTcpSocket::SocketError socketError);
};

#endif // REGISTERFORM_H
