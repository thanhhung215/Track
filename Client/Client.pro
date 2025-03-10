QT       += core gui
QT       += network
QT       += multimedia
QT       += multimediawidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ClickableLabel.cpp \
    info.cpp \
    main.cpp \
    mainwindow.cpp \
    registerform.cpp \
    statusform.cpp \
    clickablelabel.cpp

HEADERS += \
    ClickableLabel.h \
    info.h \
    mainwindow.h \
    registerform.h \
    statusform.h \
    clickablelabel.h

FORMS += \
    info.ui \
    mainwindow.ui \
    registerform.ui \
    statusform.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    icon.qrc \
    icon.qrc
