#pragma once

#include <extensionsystem/iplugin.h>

class QMainWindow;
namespace Core {
namespace Internal {

//class HelloMode;

class CorePlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.plugindemo" FILE "Core.json")

public:
    CorePlugin();
    ~CorePlugin() override;

    bool initialize(const QStringList &arguments, QString *errorMessage) override;
    void extensionsInitialized() override;
    bool delayedInitialize() override;
//    HelloMode *m_helloMode = nullptr;

    QMainWindow* mainWindow{nullptr};
};

} // namespace Internal
} // namespace HelloWorld
