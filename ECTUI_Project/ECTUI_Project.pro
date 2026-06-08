QT       += core gui xml sql  # 用于加载对xml和sql的支�?
QT += concurrent
QT += printsupport
QT += serialport
QT += network    # 添加网络支持

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    customplot.cpp \
    dataacquisitionthread.cpp \
    devicemanager.cpp \
    main.cpp \
    mainwindow.cpp \
    probe.cpp \
    probeconfigdialog.cpp \
    probemanager.cpp \
    qcustomplot.cpp \
    savemanager.cpp

HEADERS += \
    customplot.h \
    dataacquisitionthread.h \
    devicemanager.h \
    framebuffer.h \
    mainwindow.h \
    probe.h \
    probeconfigdialog.h \
    probemanager.h \
    qcustomplot.h \
    phaserotator.h \
    savemanager.h

FORMS += \
    mainwindow.ui


win32 {
    msvc: QMAKE_CXXFLAGS += /utf-8
}

msvc {
    QMAKE_CFLAGS += /utf-8
    msvc: QMAKE_CXXFLAGS += /utf-8
}



# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

