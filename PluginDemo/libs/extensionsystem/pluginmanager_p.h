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

#pragma once

#include "pluginspec.h"
#include "pluginmanager.h"

//#include <utils/algorithm.h>

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QReadWriteLock>
#include <QScopedPointer>
#include <QSet>
#include <QStringList>
#include <QWaitCondition>

#include <queue>
#include <atomic>

QT_BEGIN_NAMESPACE
class QTime;
class QTimer;
class QEventLoop;
QT_END_NAMESPACE

namespace ExtensionSystem {

class PluginManager;

namespace Internal {

class PluginSpecPrivate;

class EXTENSIONSYSTEM_EXPORT PluginManagerPrivate : public QObject
{
    Q_OBJECT
public:
    PluginManagerPrivate(PluginManager *pluginManager);
    virtual ~PluginManagerPrivate();

    // Object pool operations
    void addObject(QObject *obj);
    void removeObject(QObject *obj);

    // Plugin operations
    void loadPlugins();
    void shutdown();
    void setPluginPaths(const QStringList &paths);
    const QVector<ExtensionSystem::PluginSpec *> loadQueue();
    void loadPlugin(PluginSpec *spec, PluginSpec::State destState);
    void resolveDependencies();
    void enableDependenciesIndirectly();

    QVector<PluginSpec *> pluginSpecs;
    QStringList pluginPaths;
    QString pluginIID;
    QVector<QObject *> allObjects;      // ### make this a QVector<QPointer<QObject> > > ?
    // delayed initialization
    QTimer *delayedInitializeTimer = nullptr;
    std::queue<PluginSpec *> delayedInitializeQueue;

    PluginSpec *pluginByName(const QString &name) const;

    // used by tests
    static PluginSpec *createSpec();
    static PluginSpecPrivate *privateSpec(PluginSpec *spec);

    mutable QReadWriteLock m_lock;

    bool m_isInitializationDone = false;

private:
    PluginManager *q;

    void nextDelayedInitialize();

    void readPluginPaths();
    bool loadQueue(PluginSpec *spec,
                   QVector<ExtensionSystem::PluginSpec *> &queue,
                   QVector<ExtensionSystem::PluginSpec *> &circularityCheckQueue);
    void stopAll();
    void deleteAll();
};

} // namespace Internal
} // namespace ExtensionSystem
