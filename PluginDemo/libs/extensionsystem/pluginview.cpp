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

#include "pluginview.h"
#include "pluginmanager.h"
#include "pluginspec.h"
#include "pluginspec_p.h"

#include <../utils/algorithm.h>
#include <../utils/categorysortfiltermodel.h>
#include <../utils/utilsicons.h>
#include <../utils/itemviews.h>
#include <../utils/qtcassert.h>

#include <QDebug>
#include <QDir>
#include <QGridLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QSet>

/*!
    \class ExtensionSystem::PluginView
    \inheaderfile extensionsystem/pluginview.h
    \inmodule QtCreator

    \brief The PluginView class implements a widget that shows a list of all
    plugins and their state.

    This class can be embedded for example in a dialog in the application that
    uses the plugin manager.
    The class also provides notifications for interaction with the list.

    \sa ExtensionSystem::PluginDetailsView
    \sa ExtensionSystem::PluginErrorView
*/

/*!
    \fn void ExtensionSystem::PluginView::currentPluginChanged(ExtensionSystem::PluginSpec *spec)
    The current selection in the plugin list has changed to the
    plugin corresponding to \a spec.
*/

/*!
    \fn void ExtensionSystem::PluginView::pluginActivated(ExtensionSystem::PluginSpec *spec)
    The plugin list entry corresponding to \a spec has been activated,
    for example by a double-click.
*/

/*!
    \fn void ExtensionSystem::PluginView::pluginSettingsChanged(ExtensionSystem::PluginSpec *spec)
    The settings for the plugin list entry corresponding to \a spec changed.
*/

Q_DECLARE_METATYPE(ExtensionSystem::PluginSpec*)

using namespace Utils;

namespace ExtensionSystem {

namespace Internal {

enum Columns { NameColumn, LoadedColumn, VersionColumn, VendorColumn, };

enum IconIndex { OkIcon, ErrorIcon, NotLoadedIcon };

static const int SortRole = Qt::UserRole + 1;

static const QIcon &icon(IconIndex icon)
{
    switch (icon) {
    case OkIcon: {
        static const QIcon ok = Utils::Icons::OK.icon();
        return ok;
    }
    case ErrorIcon: {
        static const QIcon error = Utils::Icons::BROKEN.icon();
        return error;
    }
    default:
    case NotLoadedIcon: {
        static const QIcon notLoaded = Utils::Icons::NOTLOADED.icon();
        return notLoaded;
    }
    }
}

class PluginItem : public TreeItem
{
public:
    PluginItem(PluginSpec *spec, PluginView *view)
        : m_spec(spec), m_view(view)
    {}

    QVariant data(int column, int role) const override
    {
        switch (column) {
        case NameColumn:
            if (role == Qt::DisplayRole)
                return m_spec->isExperimental() ? PluginView::tr("%1 (experimental)").arg(m_spec->name())
                                                : m_spec->name();
            if (role == SortRole)
                return m_spec->name();
            if (role == Qt::ToolTipRole) {
                QString toolTip;
                if (!m_spec->isAvailableForHostPlatform())
                    toolTip = PluginView::tr("Path: %1\nPlugin is not available on this platform.");
                else if (m_spec->isEnabledIndirectly())
                    toolTip = PluginView::tr("Path: %1\nPlugin is enabled as dependency of an enabled plugin.");
                else if (m_spec->isForceEnabled())
                    toolTip = PluginView::tr("Path: %1\nPlugin is enabled by command line argument.");
                else if (m_spec->isForceDisabled())
                    toolTip = PluginView::tr("Path: %1\nPlugin is disabled by command line argument.");
                else
                    toolTip = PluginView::tr("Path: %1");
                return toolTip.arg(QDir::toNativeSeparators(m_spec->filePath()));
            }
            if (role == Qt::DecorationRole) {
                bool ok = !m_spec->hasError();
                QIcon i = icon(ok ? OkIcon : ErrorIcon);
                if (ok && m_spec->state() != PluginSpec::Running)
                    i = icon(NotLoadedIcon);
                return i;
            }
            break;

        case LoadedColumn:
            if (!m_spec->isAvailableForHostPlatform()) {
                if (role == Qt::CheckStateRole || role == SortRole)
                    return Qt::Unchecked;
                if (role == Qt::ToolTipRole)
                    return PluginView::tr("Plugin is not available on this platform.");
            } else if (m_spec->isRequired()) {
                if (role == Qt::CheckStateRole || role == SortRole)
                    return Qt::Checked;
                if (role == Qt::ToolTipRole)
                    return PluginView::tr("Plugin is required.");
            } else {
                if (role == Qt::CheckStateRole || role == SortRole)
                    return m_spec->isEnabledBySettings() ? Qt::Checked : Qt::Unchecked;
                if (role == Qt::ToolTipRole)
                    return PluginView::tr("Load on startup");
            }
            break;

        case VersionColumn:
            if (role == Qt::DisplayRole || role == SortRole)
                return QString::fromLatin1("%1 (%2)").arg(m_spec->version(), m_spec->compatVersion());
            break;

        case VendorColumn:
            if (role == Qt::DisplayRole || role == SortRole)
                return m_spec->vendor();
            break;
        }

        return QVariant();
    }

    bool setData(int column, const QVariant &data, int role) override
    {
        if (column == LoadedColumn && role == Qt::CheckStateRole)
            return m_view->setPluginsEnabled({m_spec}, data.toBool());
        return false;
    }

    bool isEnabled() const
    {
        return m_spec->isAvailableForHostPlatform() && !m_spec->isRequired();
    }

    Qt::ItemFlags flags(int column) const override
    {
        Qt::ItemFlags ret = Qt::ItemIsSelectable;

        if (isEnabled())
            ret |= Qt::ItemIsEnabled;

        if (column == LoadedColumn) {
            if (m_spec->isAvailableForHostPlatform() && !m_spec->isRequired())
                ret |= Qt::ItemIsUserCheckable;
        }

        return ret;
    }

public:
    PluginSpec *m_spec; // Not owned.
    PluginView *m_view; // Not owned.
};

class CollectionItem : public TreeItem
{
public:
    CollectionItem(const QString &name, const QVector<PluginSpec *> &plugins, PluginView *view)
        : m_name(name)
        , m_plugins(plugins)
        , m_view(view)
    {
        for (PluginSpec *spec : plugins)
            appendChild(new PluginItem(spec, view));
    }

    QVariant data(int column, int role) const override
    {
        if (column == NameColumn) {
            if (role == Qt::DisplayRole || role == SortRole)
                return m_name;
        }

        if (column == LoadedColumn) {
            if (role == Qt::ToolTipRole)
                return PluginView::tr("Load on Startup");
            if (role == Qt::CheckStateRole || role == SortRole) {
                int checkedCount = 0;
                for (PluginSpec *spec : m_plugins) {
                    if (spec->isEnabledBySettings())
                        ++checkedCount;
                }

                if (checkedCount == 0)
                    return Qt::Unchecked;
                if (checkedCount == m_plugins.length())
                    return Qt::Checked;
                return Qt::PartiallyChecked;
            }
        }

        return QVariant();
    }

    bool setData(int column, const QVariant &data, int role) override
    {
        if (column == LoadedColumn && role == Qt::CheckStateRole) {
            const QVector<PluginSpec *> affectedPlugins
                = Utils::filtered(m_plugins, [](PluginSpec *spec) { return !spec->isRequired(); });
            if (m_view->setPluginsEnabled(Utils::transform<QSet>(affectedPlugins,
                                                                 [](PluginSpec *s) { return s; }),
                                          data.toBool())) {
                update();
                return true;
            }
        }
        return false;
    }

    Qt::ItemFlags flags(int column) const override
    {
        Qt::ItemFlags ret = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        if (column == LoadedColumn)
            ret |= Qt::ItemIsUserCheckable;
        return ret;
    }

public:
    QString m_name;
    const QVector<PluginSpec *> m_plugins;
    PluginView *m_view; // Not owned.
};

} // Internal

using namespace ExtensionSystem::Internal;

/*!
    Constructs a plugin view with \a parent that displays a list of plugins
    from a plugin manager.
*/
PluginView::PluginView(QWidget *parent)
    : QWidget(parent)
{
    m_categoryView = new TreeView(this);
    m_categoryView->setAlternatingRowColors(true);
    m_categoryView->setIndentation(20);
    m_categoryView->setUniformRowHeights(true);
    m_categoryView->setSortingEnabled(true);
    m_categoryView->setColumnWidth(LoadedColumn, 40);
    m_categoryView->header()->setDefaultSectionSize(120);
    m_categoryView->header()->setMinimumSectionSize(35);
    m_categoryView->header()->setSortIndicator(0, Qt::AscendingOrder);
    m_categoryView->setActivationMode(DoubleClickActivation);
    m_categoryView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_categoryView->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_model = new TreeModel<TreeItem, CollectionItem, PluginItem>(this);
    m_model->setHeader({ tr("Name"), tr("Load"), tr("Version"), tr("Vendor") });

    m_sortModel = new CategorySortFilterModel(this);
    m_sortModel->setSourceModel(m_model);
    m_sortModel->setSortRole(SortRole);
    m_sortModel->setFilterKeyColumn(-1/*all*/);
    m_categoryView->setModel(m_sortModel);

    auto *gridLayout = new QGridLayout(this);
    gridLayout->setContentsMargins(2, 2, 2, 2);
    gridLayout->addWidget(m_categoryView, 1, 0, 1, 1);

    QHeaderView *header = m_categoryView->header();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    connect(PluginManager::instance(), &PluginManager::pluginsChanged,
            this, &PluginView::updatePlugins);

    connect(m_categoryView, &QAbstractItemView::activated,
            [this](const QModelIndex &idx) { emit pluginActivated(pluginForIndex(idx)); });

    connect(m_categoryView->selectionModel(), &QItemSelectionModel::currentChanged,
            [this](const QModelIndex &idx) { emit currentPluginChanged(pluginForIndex(idx)); });

    updatePlugins();
}

/*!
    \internal
*/
PluginView::~PluginView() = default;

/*!
    Returns the current selection in the list of plugins.
*/
PluginSpec *PluginView::currentPlugin() const
{
    return pluginForIndex(m_categoryView->currentIndex());
}

/*!
    Sets the \a filter for listing plugins.
*/
void PluginView::setFilter(const QString &filter)
{
    m_sortModel->setFilterRegularExpression(
        QRegularExpression(QRegularExpression::escape(filter),
                           QRegularExpression::CaseInsensitiveOption));
    m_categoryView->expandAll();
}

PluginSpec *PluginView::pluginForIndex(const QModelIndex &index) const
{
    const QModelIndex &sourceIndex = m_sortModel->mapToSource(index);
    PluginItem *item = m_model->itemForIndexAtLevel<2>(sourceIndex);
    return item ? item->m_spec: nullptr;
}

void PluginView::updatePlugins()
{
    // Model.
    m_model->clear();

    const QHash<QString, QVector<PluginSpec *>> pluginCollections
        = PluginManager::pluginCollections();
    std::vector<CollectionItem *> collections;
    const auto end = pluginCollections.cend();
    for (auto it = pluginCollections.cbegin(); it != end; ++it) {
        const QString name = it.key().isEmpty() ? tr("Utilities") : it.key();
        collections.push_back(new CollectionItem(name, it.value(), this));
    }
    Utils::sort(collections, &CollectionItem::m_name);

    for (CollectionItem *collection : qAsConst(collections))
        m_model->rootItem()->appendChild(collection);

    emit m_model->layoutChanged();
    m_categoryView->expandAll();
}

static QString pluginListString(const QSet<PluginSpec *> &plugins)
{
    QStringList names = Utils::transform<QList>(plugins, &PluginSpec::name);
    names.sort();
    return names.join(QLatin1Char('\n'));
}

bool PluginView::setPluginsEnabled(const QSet<PluginSpec *> &plugins, bool enable)
{
    QSet<PluginSpec *> additionalPlugins;
    if (enable) {
        for (PluginSpec *spec : plugins) {
            for (PluginSpec *other : PluginManager::pluginsRequiredByPlugin(spec)) {
                if (!other->isEnabledBySettings())
                    additionalPlugins.insert(other);
            }
        }
        additionalPlugins.subtract(plugins);
        if (!additionalPlugins.isEmpty()) {
            if (QMessageBox::question(this, tr("Enabling Plugins"),
                                      tr("Enabling\n%1\nwill also enable the following plugins:\n\n%2")
                                      .arg(pluginListString(plugins))
                                      .arg(pluginListString(additionalPlugins)),
                                      QMessageBox::Ok | QMessageBox::Cancel,
                                      QMessageBox::Ok) != QMessageBox::Ok)
                return false;
        }
    } else {
        for (PluginSpec *spec : plugins) {
            for (PluginSpec *other : PluginManager::pluginsRequiringPlugin(spec)) {
                if (other->isEnabledBySettings())
                    additionalPlugins.insert(other);
            }
        }
        additionalPlugins.subtract(plugins);
        if (!additionalPlugins.isEmpty()) {
            if (QMessageBox::question(this, tr("Disabling Plugins"),
                                      tr("Disabling\n%1\nwill also disable the following plugins:\n\n%2")
                                      .arg(pluginListString(plugins))
                                      .arg(pluginListString(additionalPlugins)),
                                      QMessageBox::Ok | QMessageBox::Cancel,
                                      QMessageBox::Ok) != QMessageBox::Ok)
                return false;
        }
    }

    const QSet<PluginSpec *> affectedPlugins = plugins + additionalPlugins;
    for (PluginSpec *spec : affectedPlugins) {
        PluginItem *item = m_model->findItemAtLevel<2>([spec](PluginItem *item) {
                return item->m_spec == spec;
        });
        QTC_ASSERT(item, continue);
        spec->d->setEnabledBySettings(enable);
        item->updateColumn(LoadedColumn);
        item->parent()->updateColumn(LoadedColumn);
        emit pluginSettingsChanged(spec);
    }
    return true;
}

} // namespace ExtensionSystem
