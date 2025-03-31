#pragma once

#include "extensionsystem_global.h"
#include "qreadwritelock.h"

#include <QObject>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QTextStream;
QT_END_NAMESPACE

namespace ExtensionSystem {
class IPlugin;
class PluginSpec;

namespace Internal { class PluginManagerPrivate; }

class EXTENSIONSYSTEM_EXPORT PluginManager : public QObject
{
    Q_OBJECT

public:
    static PluginManager *instance();

    PluginManager();
    virtual ~PluginManager();

    // Object pool operations
    static void addObject(QObject *obj);
    static void removeObject(QObject *obj);
    static QVector<QObject *> allObjects();
    static QReadWriteLock *listLock();

    // This is useful for soft dependencies using pure interfaces.
    template <typename T> static T *getObject()
    {
        QReadLocker lock(listLock());
        const QVector<QObject *> all = allObjects();
        for (QObject *obj : all) {
            if (T *result = qobject_cast<T *>(obj))
                return result;
        }
        return nullptr;
    }
    template <typename T, typename Predicate> static T *getObject(Predicate predicate)
    {
        QReadLocker lock(listLock());
        const QVector<QObject *> all = allObjects();
        for (QObject *obj : all) {
            if (T *result = qobject_cast<T *>(obj))
                if (predicate(result))
                    return result;
        }
        return 0;
    }

    static QObject *getObjectByName(const QString &name);

    // Plugin operations
    static QVector<PluginSpec *> loadQueue();
    static void loadPlugins();
    static QStringList pluginPaths();
    static void setPluginPaths(const QStringList &paths);
    static QString pluginIID();
    static void setPluginIID(const QString &iid);
    static const QVector<PluginSpec *> plugins();
    static bool hasError();
    static const QStringList allErrors();
    static const QSet<PluginSpec *> pluginsRequiringPlugin(PluginSpec *spec);
    static const QSet<PluginSpec *> pluginsRequiredByPlugin(PluginSpec *spec);


    static bool isInitializationDone();

    static void shutdown();

signals:
    void objectAdded(QObject *obj);
    void aboutToRemoveObject(QObject *obj);

    void pluginsChanged();
    void initializationDone();

    friend class Internal::PluginManagerPrivate;
};

} // namespace ExtensionSystem
