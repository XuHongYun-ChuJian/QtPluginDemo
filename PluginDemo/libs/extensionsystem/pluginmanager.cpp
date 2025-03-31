/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "pluginmanager.h"
#include "pluginmanager_p.h"
#include "pluginspec.h"
#include "pluginspec_p.h"
#include "iplugin.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QLibrary>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QMetaProperty>
#include <QPushButton>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QWriteLocker>

#include <../utils/algorithm.h>

#include <functional>
#include <memory>

Q_LOGGING_CATEGORY(pluginLog, "qtc.extensionsystem", QtWarningMsg)

const int DELAYED_INITIALIZE_INTERVAL = 20; // ms

enum { debugLeaks = 0 };

using namespace Utils;

namespace ExtensionSystem {

using namespace Internal;

static Internal::PluginManagerPrivate *d = nullptr;
static PluginManager *m_instance = nullptr;

/*!
    Gets the unique plugin manager instance.
*/
PluginManager *PluginManager::instance()
{
    return m_instance;
}

/*!
    Creates a plugin manager. Should be done only once per application.
*/
PluginManager::PluginManager()
{
    m_instance = this;
    d = new PluginManagerPrivate(this);
}

/*!
    \internal
*/
PluginManager::~PluginManager()
{
    delete d;
    d = nullptr;
}

/*!
    Adds the object \a obj to the object pool, so it can be retrieved
    again from the pool by type.

    The plugin manager does not do any memory management. Added objects
    must be removed from the pool and deleted manually by whoever is responsible for the object.

    Emits the \c objectAdded() signal.

    \sa PluginManager::removeObject()
    \sa PluginManager::getObject()
    \sa PluginManager::getObjectByName()
*/
void PluginManager::addObject(QObject *obj)
{
    d->addObject(obj);
}

/*!
    Emits the \c aboutToRemoveObject() signal and removes the object \a obj
    from the object pool.
    \sa PluginManager::addObject()
*/
void PluginManager::removeObject(QObject *obj)
{
    d->removeObject(obj);
}

/*!
    Retrieves the list of all objects in the pool, unfiltered.

    Usually, clients do not need to call this function.

    \sa PluginManager::getObject()
*/
QVector<QObject *> PluginManager::allObjects()
{
    return d->allObjects;
}

/*!
    \internal
*/
QReadWriteLock *PluginManager::listLock()
{
    return &d->m_lock;
}

/*!
    Tries to load all the plugins that were previously found when
    setting the plugin search paths. The plugin specs of the plugins
    can be used to retrieve error and state information about individual plugins.

    \sa setPluginPaths()
    \sa plugins()
*/
void PluginManager::loadPlugins()
{
    d->loadPlugins();
}

/*!
    Returns \c true if any plugin has errors even though it is enabled.
    Most useful to call after loadPlugins().
*/
bool PluginManager::hasError()
{
    return Utils::anyOf(plugins(), [](PluginSpec *spec) {
        // only show errors on startup if plugin is enabled.
        return spec->hasError() && spec->isEffectivelyEnabled();
    });
}

const QStringList PluginManager::allErrors()
{
    return Utils::transform<QStringList>(Utils::filtered(plugins(), [](const PluginSpec *spec) {
        return spec->hasError() && spec->isEffectivelyEnabled();
    }), [](const PluginSpec *spec) {
        return spec->name().append(": ").append(spec->errorString());
    });
}

/*!
    Returns all plugins that require \a spec to be loaded. Recurses into dependencies.
 */
const QSet<PluginSpec *> PluginManager::pluginsRequiringPlugin(PluginSpec *spec)
{
    QSet<PluginSpec *> dependingPlugins({spec});
    // recursively add plugins that depend on plugins that.... that depend on spec
    for (PluginSpec *spec : d->loadQueue()) {
        if (spec->requiresAny(dependingPlugins))
            dependingPlugins.insert(spec);
    }
    dependingPlugins.remove(spec);
    return dependingPlugins;
}

/*!
    Returns all plugins that \a spec requires to be loaded. Recurses into dependencies.
 */
const QSet<PluginSpec *> PluginManager::pluginsRequiredByPlugin(PluginSpec *spec)
{
    QSet<PluginSpec *> recursiveDependencies;
    recursiveDependencies.insert(spec);
    std::queue<PluginSpec *> queue;
    queue.push(spec);
    while (!queue.empty()) {
        PluginSpec *checkSpec = queue.front();
        queue.pop();
        const QHash<PluginDependency, PluginSpec *> deps = checkSpec->dependencySpecs();
        for (auto depIt = deps.cbegin(), end = deps.cend(); depIt != end; ++depIt) {
            if (depIt.key().type != PluginDependency::Required)
                continue;
            PluginSpec *depSpec = depIt.value();
            if (!recursiveDependencies.contains(depSpec)) {
                recursiveDependencies.insert(depSpec);
                queue.push(depSpec);
            }
        }
    }
    recursiveDependencies.remove(spec);
    return recursiveDependencies;
}

static QString filled(const QString &s, int min)
{
    return s + QString(qMax(0, min - s.size()), ' ');
}

/*!
    The list of paths were the plugin manager searches for plugins.

    \sa setPluginPaths()
*/
QStringList PluginManager::pluginPaths()
{
    return d->pluginPaths;
}

/*!
    Sets the plugin paths. All the specified \a paths and their subdirectory
    trees are searched for plugins.

    \sa pluginPaths()
    \sa loadPlugins()
*/
void PluginManager::setPluginPaths(const QStringList &paths)
{
    d->setPluginPaths(paths);
}

/*!
    The IID that valid plugins must have.

    \sa setPluginIID()
*/
QString PluginManager::pluginIID()
{
    return d->pluginIID;
}

/*!
    Sets the IID that valid plugins must have to \a iid. Only plugins with this
    IID are loaded, others are silently ignored.

    At the moment this must be called before setPluginPaths() is called.

    \omit
    // ### TODO let this + setPluginPaths read the plugin meta data lazyly whenever loadPlugins() or plugins() is called.
    \endomit
*/
void PluginManager::setPluginIID(const QString &iid)
{
    d->pluginIID = iid;
}

/*!
    List of all plugins that have been found in the plugin search paths.
    This list is valid directly after the setPluginPaths() call.
    The plugin specifications contain plugin metadata and the current state
    of the plugins. If a plugin's library has been already successfully loaded,
    the plugin specification has a reference to the created plugin instance as well.

    \sa setPluginPaths()
*/
const QVector<PluginSpec *> PluginManager::plugins()
{
    return d->pluginSpecs;
}

/* Extract a sublist from the serialized arguments
 * indicated by a keyword starting with a colon indicator:
 * ":a,i1,i2,:b:i3,i4" with ":a" -> "i1,i2"
 */
static QStringList subList(const QStringList &in, const QString &key)
{
    QStringList rc;
    // Find keyword and copy arguments until end or next keyword
    const QStringList::const_iterator inEnd = in.constEnd();
    QStringList::const_iterator it = std::find(in.constBegin(), inEnd, key);
    if (it != inEnd) {
        const QChar nextIndicator = QLatin1Char(':');
        for (++it; it != inEnd && !it->startsWith(nextIndicator); ++it)
            rc.append(*it);
    }
    return rc;
}

static inline void indent(QTextStream &str, int indent)
{
    str << QString(indent, ' ');
}

/*!
    Returns a list of plugins in load order.
*/
QVector<PluginSpec *> PluginManager::loadQueue()
{
    return d->loadQueue();
}

//============PluginManagerPrivate===========

/*!
    \internal
*/
PluginSpec *PluginManagerPrivate::createSpec()
{
    return new PluginSpec();
}

/*!
    \internal
*/
PluginSpecPrivate *PluginManagerPrivate::privateSpec(PluginSpec *spec)
{
    return spec->d;
}

void PluginManagerPrivate::nextDelayedInitialize()
{
    while (!delayedInitializeQueue.empty()) {
        PluginSpec *spec = delayedInitializeQueue.front();
        delayedInitializeQueue.pop();
        bool delay = spec->d->delayedInitialize();
        if (delay)
            break; // do next delayedInitialize after a delay
    }
    if (delayedInitializeQueue.empty()) {
        m_isInitializationDone = true;
        delete delayedInitializeTimer;
        delayedInitializeTimer = nullptr;
        emit q->initializationDone();
    } else {
        delayedInitializeTimer->start();
    }
}

/*!
    \internal
*/
PluginManagerPrivate::PluginManagerPrivate(PluginManager *pluginManager) :
    q(pluginManager)
{
}


/*!
    \internal
*/
PluginManagerPrivate::~PluginManagerPrivate()
{
    qDeleteAll(pluginSpecs);
}

/*!
    \internal
*/
void PluginManagerPrivate::stopAll()
{
    if (delayedInitializeTimer && delayedInitializeTimer->isActive()) {
        delayedInitializeTimer->stop();
        delete delayedInitializeTimer;
        delayedInitializeTimer = nullptr;
    }

    const QVector<PluginSpec *> queue = loadQueue();
    for (PluginSpec *spec : queue)
        loadPlugin(spec, PluginSpec::Stopped);
}

/*!
    \internal
*/
void PluginManagerPrivate::deleteAll()
{
    Utils::reverseForeach(loadQueue(), [this](PluginSpec *spec) {
        loadPlugin(spec, PluginSpec::Deleted);
    });
}

/*!
    \internal
*/
void PluginManagerPrivate::addObject(QObject *obj)
{
    {
        QWriteLocker lock(&m_lock);
        if (obj == nullptr) {
            qWarning() << "PluginManagerPrivate::addObject(): trying to add null object";
            return;
        }
        if (allObjects.contains(obj)) {
            qWarning() << "PluginManagerPrivate::addObject(): trying to add duplicate object";
            return;
        }

        if (debugLeaks)
            qDebug() << "PluginManagerPrivate::addObject" << obj << obj->objectName();

        allObjects.append(obj);
    }
    emit q->objectAdded(obj);
}

/*!
    \internal
*/
void PluginManagerPrivate::removeObject(QObject *obj)
{
    if (obj == nullptr) {
        qWarning() << "PluginManagerPrivate::removeObject(): trying to remove null object";
        return;
    }

    if (!allObjects.contains(obj)) {
        qWarning() << "PluginManagerPrivate::removeObject(): object not in list:"
            << obj << obj->objectName();
        return;
    }
    if (debugLeaks)
        qDebug() << "PluginManagerPrivate::removeObject" << obj << obj->objectName();

    emit q->aboutToRemoveObject(obj);
    QWriteLocker lock(&m_lock);
    allObjects.removeAll(obj);
}

/*!
    \internal
*/
void PluginManagerPrivate::loadPlugins()
{
    const QVector<PluginSpec *> queue = loadQueue();

    for (PluginSpec *spec : queue)
        loadPlugin(spec, PluginSpec::Loaded);

    for (PluginSpec *spec : queue)
        loadPlugin(spec, PluginSpec::Initialized);

    Utils::reverseForeach(queue, [this](PluginSpec *spec) {
        loadPlugin(spec, PluginSpec::Running);
        if (spec->state() == PluginSpec::Running) {
            delayedInitializeQueue.push(spec);
        } else {
            // Plugin initialization failed, so cleanup after it
            spec->d->kill();
        }
    });
    emit q->pluginsChanged();

    delayedInitializeTimer = new QTimer;
    delayedInitializeTimer->setInterval(DELAYED_INITIALIZE_INTERVAL);
    delayedInitializeTimer->setSingleShot(true);
    connect(delayedInitializeTimer, &QTimer::timeout,
            this, &PluginManagerPrivate::nextDelayedInitialize);
    delayedInitializeTimer->start();
}

void PluginManagerPrivate::shutdown()
{
    stopAll();
    deleteAll();
    if (!allObjects.isEmpty()) {
        qDebug() << "There are" << allObjects.size() << "objects left in the plugin manager pool.";
        // Intentionally split debug info here, since in case the list contains
        // already deleted object we get at least the info about the number of objects;
        qDebug() << "The following objects left in the plugin manager pool:" << allObjects;
    }
}
/*!
    \internal
*/
const QVector<PluginSpec *> PluginManagerPrivate::loadQueue()
{
    QVector<PluginSpec *> queue;
    for (PluginSpec *spec : qAsConst(pluginSpecs)) {
        QVector<PluginSpec *> circularityCheckQueue;
        loadQueue(spec, queue, circularityCheckQueue);
    }
    return queue;
}

/*!
    \internal
*/
bool PluginManagerPrivate::loadQueue(PluginSpec *spec,
                                     QVector<PluginSpec *> &queue,
                                     QVector<PluginSpec *> &circularityCheckQueue)
{
    if (queue.contains(spec))
        return true;
    // check for circular dependencies
    if (circularityCheckQueue.contains(spec)) {
        spec->d->hasError = true;
        spec->d->errorString = PluginManager::tr("Circular dependency detected:");
        spec->d->errorString += QLatin1Char('\n');
        int index = circularityCheckQueue.indexOf(spec);
        for (int i = index; i < circularityCheckQueue.size(); ++i) {
            spec->d->errorString.append(PluginManager::tr("%1 (%2) depends on")
                .arg(circularityCheckQueue.at(i)->name()).arg(circularityCheckQueue.at(i)->version()));
            spec->d->errorString += QLatin1Char('\n');
        }
        spec->d->errorString.append(PluginManager::tr("%1 (%2)").arg(spec->name()).arg(spec->version()));
        return false;
    }
    circularityCheckQueue.append(spec);
    // check if we have the dependencies
    if (spec->state() == PluginSpec::Invalid || spec->state() == PluginSpec::Read) {
        queue.append(spec);
        return false;
    }

    // add dependencies
    const QHash<PluginDependency, PluginSpec *> deps = spec->dependencySpecs();
    for (auto it = deps.cbegin(), end = deps.cend(); it != end; ++it) {
        // Skip test dependencies since they are not real dependencies but just force-loaded
        // plugins when running tests
        if (it.key().type == PluginDependency::Test)
            continue;
        PluginSpec *depSpec = it.value();
        if (!loadQueue(depSpec, queue, circularityCheckQueue)) {
            spec->d->hasError = true;
            spec->d->errorString =
                PluginManager::tr("Cannot load plugin because dependency failed to load: %1 (%2)\nReason: %3")
                    .arg(depSpec->name()).arg(depSpec->version()).arg(depSpec->errorString());
            return false;
        }
    }
    // add self
    queue.append(spec);
    return true;
}

/*!
    \internal
*/
void PluginManagerPrivate::loadPlugin(PluginSpec *spec, PluginSpec::State destState)
{
    if (spec->hasError() || spec->state() != destState-1)
        return;

    // don't load disabled plugins.
    if (!spec->isEffectivelyEnabled() && destState == PluginSpec::Loaded)
        return;

    switch (destState) {
    case PluginSpec::Running:
        spec->d->initializeExtensions();
        return;
    case PluginSpec::Deleted:
        spec->d->kill();
        return;
    default:
        break;
    }
    // check if dependencies have loaded without error
    const QHash<PluginDependency, PluginSpec *> deps = spec->dependencySpecs();
    for (auto it = deps.cbegin(), end = deps.cend(); it != end; ++it) {
        if (it.key().type != PluginDependency::Required)
            continue;
        PluginSpec *depSpec = it.value();
        if (depSpec->state() != destState) {
            spec->d->hasError = true;
            spec->d->errorString =
                PluginManager::tr("Cannot load plugin because dependency failed to load: %1(%2)\nReason: %3")
                    .arg(depSpec->name()).arg(depSpec->version()).arg(depSpec->errorString());
            return;
        }
    }
    switch (destState) {
    case PluginSpec::Loaded:
        spec->d->loadLibrary();
        break;
    case PluginSpec::Initialized:
        spec->d->initializePlugin();
        break;
    case PluginSpec::Stopped:
        break;
    default:
        break;
    }
}

/*!
    \internal
*/
void PluginManagerPrivate::setPluginPaths(const QStringList &paths)
{
    qCDebug(pluginLog) << "Plugin search paths:" << paths;
    qCDebug(pluginLog) << "Required IID:" << pluginIID;
    pluginPaths = paths;
    readPluginPaths();
}

static const QStringList pluginFiles(const QStringList &pluginPaths)
{
    QStringList pluginFiles;
    QStringList searchPaths = pluginPaths;
    while (!searchPaths.isEmpty()) {
        const QDir dir(searchPaths.takeFirst());
        const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoSymLinks);
        const QStringList absoluteFilePaths = Utils::transform(files, &QFileInfo::absoluteFilePath);
        pluginFiles += Utils::filtered(absoluteFilePaths, [](const QString &path) { return QLibrary::isLibrary(path); });
        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot);
        searchPaths += Utils::transform(dirs, &QFileInfo::absoluteFilePath);
    }
    return pluginFiles;
}

/*!
    \internal
*/
void PluginManagerPrivate::readPluginPaths()
{
    qDeleteAll(pluginSpecs);
    pluginSpecs.clear();

    for (const QString &pluginFile : pluginFiles(pluginPaths)) {
        PluginSpec *spec = PluginSpec::read(pluginFile);
        if (!spec) // not a Qt Creator plugin
            continue;

        pluginSpecs.append(spec);
    }
    resolveDependencies();
    enableDependenciesIndirectly();
    // ensure deterministic plugin load order by sorting
    Utils::sort(pluginSpecs, &PluginSpec::name);
    emit q->pluginsChanged();
}

void PluginManagerPrivate::resolveDependencies()
{
    for (PluginSpec *spec : qAsConst(pluginSpecs))
        spec->d->resolveDependencies(pluginSpecs);
}

void PluginManagerPrivate::enableDependenciesIndirectly()
{
    for (PluginSpec *spec : qAsConst(pluginSpecs))
        spec->d->enabledIndirectly = false;
    // cannot use reverse loadQueue here, because test dependencies can introduce circles
    QVector<PluginSpec *> queue = Utils::filtered(pluginSpecs, &PluginSpec::isEffectivelyEnabled);
    while (!queue.isEmpty()) {
        PluginSpec *spec = queue.takeFirst();
        queue += spec->d->enableDependenciesIndirectly(false);
    }
}

PluginSpec *PluginManagerPrivate::pluginByName(const QString &name) const
{
    return Utils::findOrDefault(pluginSpecs, [name](PluginSpec *spec) { return spec->name() == name; });
}

bool PluginManager::isInitializationDone()
{
    return d->m_isInitializationDone;
}

void PluginManager::shutdown()
{
     d->shutdown();
}

/*!
    Retrieves one object with \a name from the object pool.
    \sa addObject()
*/

QObject *PluginManager::getObjectByName(const QString &name)
{
    QReadLocker lock(&d->m_lock);
    return Utils::findOrDefault(allObjects(), [&name](const QObject *obj) {
        return obj->objectName() == name;
    });
}

} // ExtensionSystem
