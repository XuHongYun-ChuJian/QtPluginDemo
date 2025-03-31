#include "iplugin.h"
#include "iplugin_p.h"
#include "pluginmanager.h"
#include "pluginspec.h"

using namespace ExtensionSystem;

/*!
    \internal
*/
IPlugin::IPlugin()
    : d(new Internal::IPluginPrivate())
{
}

/*!
    \internal
*/
IPlugin::~IPlugin()
{
    delete d;
    d = nullptr;
}

/*!
    Returns the PluginSpec corresponding to this plugin.
    This is not available in the constructor.
*/
PluginSpec *IPlugin::pluginSpec() const
{
    return d->pluginSpec;
}
