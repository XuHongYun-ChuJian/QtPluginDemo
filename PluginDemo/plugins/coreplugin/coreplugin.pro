include($$PWD/../../project_path.pri)
include($$PWD/core_dependencies.pri)

QT += core widgets

TEMPLATE = lib
CONFIG += plugin

TARGET = Core

DESTDIR = $$BUILD_TREE/bin/plugins

HEADERS += coreplugin.h
SOURCES += coreplugin.cpp

LIBS += -L$$BUILD_TREE/libs -lExtensionSystem

QMAKE_SUBSTITUTES += Core.json.in
DISTFILES += \
    Core.json.in
