include($$PWD/../project_path.pri)

DESTDIR = $$BUILD_TREE/bin

TEMPLATE = app
CONFIG += console c++17

QT += core widgets

SOURCES += \
        main.cpp

LIBS += -L$$BUILD_TREE/bin -lExtensionSystem
