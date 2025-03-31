include($$PWD/../../project_path.pri)

DESTDIR = $$BUILD_TREE/bin

DEFINES += EXTENSIONSYSTEM_LIBRARY

TARGET = ExtensionSystem
TEMPLATE = lib
CONFIG += shared dll

CONFIG += c++17 c++1z

QT += core widgets concurrent

INCLUDEPATH += $$PWD/../

HEADERS += \
#    pluginerrorview.h \
#    plugindetailsview.h \
#    invoker.h \
    iplugin.h \
    iplugin_p.h \
    extensionsystem_global.h \
    pluginmanager.h \
    pluginmanager_p.h \
    pluginspec.h \
    pluginspec_p.h
#    pluginview.h \
#    optionsparser.h \
#    pluginerroroverview.h
SOURCES += \
#    pluginerrorview.cpp \
#    plugindetailsview.cpp \
#    invoker.cpp \
    iplugin.cpp \
    pluginmanager.cpp \
    pluginspec.cpp
#    pluginview.cpp \
#    optionsparser.cpp \
#    pluginerroroverview.cpp
FORMS += \
#    pluginerrorview.ui \
#    plugindetailsview.ui \
#    pluginerroroverview.ui

# contails Utils file
HEADERS += \
    $$PWD/../utils/algorithm.h \
    $$PWD/../utils/stringutils.h \
    $$PWD/../utils/qtcassert.h

SOURCES += \
    $$PWD/../utils/stringutils.cpp \
    $$PWD/../utils/qtcassert.cpp
