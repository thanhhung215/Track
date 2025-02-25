#ifndef INFO_H
#define INFO_H

#include <QDialog>
#include "ui_info.h"
#include <QTcpSocket>
#include "statusform.h"

// Forward declaration of statusForm class
class statusForm;

namespace Ui {
class Info;
}

class Info : public QDialog
{
    Q_OBJECT

public:
    explicit Info(QWidget *parent = nullptr, statusForm *statusF = nullptr);
    ~Info(); // Declare the destructor

private slots:

    void on_btnSubmit_clicked();
    void handleResponse();

private:
    Ui::Info *ui;
    QTcpSocket *socket; // Added this line
    statusForm *statusF; // Add this line
};

#endif // INFO_H
