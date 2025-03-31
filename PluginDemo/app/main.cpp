#include <iostream>

#include <QApplication>
#include <QDir>

#include <../libs/extensionsystem/iplugin.h>
#include <../libs/extensionsystem/pluginmanager.h>
#include <../libs/extensionsystem/pluginspec.h>

using namespace ExtensionSystem;

const char corePluginNameC[] = "Core";

int main(int argc, char **argv)
{
    QLoggingCategory::setFilterRules("qtc.extensionsystem.debug=true");

    QApplication app(argc, argv);

    const QStringList pluginPaths = {QApplication::applicationDirPath() + "/plugins"};

    PluginManager pluginManager;
    PluginManager::setPluginIID(QLatin1String("org.qt-project.plugindemo"));
    PluginManager::setPluginPaths(pluginPaths);

    const QVector<PluginSpec *> plugins = PluginManager::plugins();
    PluginSpec *coreplugin = nullptr;
    for (PluginSpec *spec : plugins) {
        if (spec->name() == QLatin1String(corePluginNameC)) {
            coreplugin = spec;
            break;
        }
    }

    if (!coreplugin) {
        QString nativePaths = QDir::toNativeSeparators(pluginPaths.join(QLatin1Char(',')));
        const QString reason = QCoreApplication::translate("Application", "Could not find Core plugin in %1").arg(nativePaths);
        qWarning() << reason;
        return 1;
    }

    if (coreplugin->hasError()) {
        qWarning() << coreplugin->errorString();
        return 1;
    }

    PluginManager::loadPlugins();
    if (coreplugin->hasError()) {
        qWarning() << coreplugin->errorString();
        return 1;
    }

    // shutdown plugin manager on the exit
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &pluginManager, &PluginManager::shutdown);


    return app.exec();
}
