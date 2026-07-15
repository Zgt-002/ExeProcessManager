QT += widgets concurrent

CONFIG += c++17
TEMPLATE = app
TARGET = ExeProcessManager

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    processmanager.cpp

HEADERS += \
    mainwindow.h \
    processmanager.h

win32 {
    DEFINES += UNICODE _UNICODE
    CONFIG += windows
    CONFIG -= console
    RC_ICONS = assets/icon/exe-process-manager-v5-light-silver.ico
}
