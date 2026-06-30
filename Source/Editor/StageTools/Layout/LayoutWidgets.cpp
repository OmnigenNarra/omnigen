#include "stdafx.h"
#include "LayoutWidgets.h"
#include "Utils/PlatformMisc.h"
#include "Omnigen.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Scene/Generation/Stages/Layout/DomainHandleDrawable.h"
#include "LayoutSelection.h"
#include "StageToolsLayout.h"
#include "OmniSatScan/OmniSatScan.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"

#include <QButtonGroup>
#include <QToolbar>
#include <QShortcut>
#include <QMenu>

namespace Design
{
    LayoutTreeItem::LayoutTreeItem(const QSharedPointer<DDomain>& domain, OutlineTreeItem* parent /*= nullptr*/)
        : OutlineTreeItem({ domain->getName() }, parent)
        , guid(domain->getGuid())
        , type(domain->getType())
    {
    }

    LayoutTreeItem::LayoutTreeItem(const EDomainType& type, OutlineTreeItem* parent /*= nullptr*/)
        : OutlineTreeItem({ QString::fromStdString(std::string(magic_enum::enum_name(type)) + " Domains") }, parent)
        , guid(-1)
        , type(type)
    {
    }

    QVariant LayoutTreeItem::getDataByRole(int role) const
    {
        if (role == Guid)
            return guid;

        if (role == Type)
            return int(type);

        return {};
    }

    QVariant QLayoutTreeModel::data(const QModelIndex& index, int role) const
    {
        if (!index.isValid())
            return QVariant();

        auto* item = static_cast<LayoutTreeItem*>(index.internalPointer());

        if (role == Qt::DisplayRole)
            return item->data(index.column());
        else if (role == Qt::ForegroundRole)
        {
            auto color = DDomain::Colors[item->type] * (item->type == EDomainType::Biome ? 2.0f : 1.3f);
            return QVariant(QColor(color.x() * 255.f, color.y() * 255.f, color.z() * 255.f));
        }

        return item->getDataByRole(role);
    }

    void QLayoutTreeModel::clearSelection()
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)) && !History::GetContext()->IsUndoingOrRedoing())
            return;

        if (treeView->selectionModel()->selection().isEmpty())
            return;

        treeView->selectionModel()->select(QModelIndex(), QItemSelectionModel::SelectionFlag::Clear);
    }

    void QLayoutTreeModel::selectDomain(qint64 guid)
    {
        auto matches = match(index(0, 0), LayoutTreeItem::CustomRoles::Guid, guid, 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        QItemSelection s(match, match);
        s.merge(treeView->selectionModel()->selection(), QItemSelectionModel::SelectionFlag::Select);
        treeView->selectionModel()->select(s, QItemSelectionModel::SelectionFlag::Select);
    }

    void QLayoutTreeModel::addDomain(size_t typeHash, QSharedPointer<Editable> object)
    {
        auto&& drawable = object.dynamicCast<OmnigenDrawable>();
        if (!drawable)
            return;

        // Ensure no duplicates
        auto matches = match(index(0, 0), LayoutTreeItem::CustomRoles::Guid, drawable->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (!matches.isEmpty())
            return;

        auto domain = object.dynamicCast<DDomain>();
        if (!domain)
            return;

        auto parent = domainCategories[domain->getType()];
        auto parentIndex = index(parent->row(), 0);

        beginInsertRows(parentIndex, parent->childCount(), parent->childCount());

        auto newItem = new LayoutTreeItem(domain, getRootItem());
        domainCategories[domain->getType()]->appendChild(newItem);

        endInsertRows();
    }

    void QLayoutTreeModel::removeDomain(QSharedPointer<Editable> object)
    {
        auto&& drawable = object.dynamicCast<OmnigenDrawable>();
        if (!drawable)
            return;

        auto matches = match(index(0, 0), LayoutTreeItem::CustomRoles::Guid, drawable->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        auto* item = static_cast<LayoutTreeItem*>(matches.front().internalPointer());

        beginRemoveRows(match.parent(), match.row(), match.row());
        item->getParentItem()->removeChild(item);
        endRemoveRows();
    }

    void QLayoutTreeModel::updateDomain(QSharedPointer<Editable> object, bool reset)
    {
        auto&& drawable = object.dynamicCast<OmnigenDrawable>();
        if (!drawable)
            return;

        auto matches = match(index(0, 0), LayoutTreeItem::CustomRoles::Guid, drawable->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        auto* item = static_cast<LayoutTreeItem*>(match.internalPointer());

        auto domain = object.dynamicCast<DDomain>();
        Q_ASSERT(domain);
        item->setData(0, domain->getName());
        item->type = domain->getType();

        emit dataChanged(match, match);
    }


    void QLayoutTreeModel::loadDomains()
    {
        beginResetModel();

        for (int i = 0; i < magic_enum::enum_count<EDomainType>() - 1; i++)
        {
            auto&& domainType = magic_enum::enum_value<EDomainType>(i);
            auto newItem = new LayoutTreeItem(domainType, getRootItem());
            domainCategories[domainType] = newItem;
            getRootItem()->appendChild(newItem);
        }

        auto&& domains = Generation::Data::get()->getAllDomains();
        for (auto&& domain : domains)
        {
            auto newItem = new LayoutTreeItem(domain.second, domainCategories[domain.second->getType()]);
            domainCategories[domain.second->getType()]->appendChild(newItem);
        }

        endResetModel();
    }

    void QLayoutTreeModel::clear()
    {
        beginResetModel();

        getRootItem()->clearChildren();
        domainCategories.clear();

        endResetModel();
    }

    void QLayoutTreeModel::setTreeView(QTreeView* inView)
    {
        treeView = inView;

        connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &QLayoutTreeModel::treeSelectionChanged);
        connect(treeView, &QTreeView::doubleClicked, this, &QLayoutTreeModel::itemDoubleClicked);
        connect(treeView, &QTreeView::customContextMenuRequested, this, &QLayoutTreeModel::outlineContextMenuRequested);
    }

    void QLayoutTreeModel::treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)))
            return;

        treeView->selectionModel()->blockSignals(true);

        LayoutSelectionMgr::get()->clearSelection();

        for (auto&& index : selected.indexes())
            if (auto* item = static_cast<LayoutTreeItem*>(index.internalPointer()); item->guid != -1)
            {
                auto domain = Generation::Data::get()->findDomainByGuid(item->guid);
                LayoutSelectionMgr::get()->setSelection<Design::ELayoutSelection::Domain>({ (*domain)->getHandle() });
            }

        treeView->selectionModel()->blockSignals(false);
    }

    void QLayoutTreeModel::itemDoubleClicked(const QModelIndex& idx)
    {
        QOmnigenViewportSection::getActiveViewport()->tryMoveToSelection();
    }

    void QLayoutTreeModel::outlineContextMenuRequested(const QPoint& pos)
    {
        QModelIndex index = treeView->indexAt(pos);

        if (auto* item = static_cast<LayoutTreeItem*>(index.internalPointer()); item && item->guid != -1)
        {
            auto domain = *Generation::Data::get()->findDomainByGuid(item->guid);

            QMenu* contextMenu = DomainSelection::requestContextMenu(domain->getHandle().lock());
            
            contextMenu->popup(treeView->viewport()->mapToGlobal(pos));
        }
    }

    QWidget* StageTools<EGenerationStage::Layout>::createOutlineToolbar()
    {
        auto* omni = Omnigen::get();

        auto* toolBar = new QToolBar();
        toolBar->setIconSize(QSize(40, 20));
        toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        auto toggleBrushAction = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Brush", this);
        toggleBrushAction->setCheckable(true);
        connect(toggleBrushAction, &QAction::triggered, this, [this, toggleBrushAction]()
            {
                bPainting = toggleBrushAction->isChecked();
                paintTool->setVisible(bPainting);
            });

        // Satellite Scan Widget
        auto satScanWindowAction = new QAction(QIcon("Resources/Icons/BiomeIcon.png"), "Satellite Widget", this);
        connect(satScanWindowAction, &QAction::triggered, this, [this, omni]()
            {
                auto&& popupSatScanWindow = OmniSatScanWindow::get(omni);
                popupSatScanWindow->show();
            });

        toolBar->addAction(QIcon("Resources/Icons/TerrainIcon.png"), "Terrain", this, [&]() {actions[isKeyDown(VK_CONTROL) ? ELayoutAction::ExtractTerrain : ELayoutAction::CreateTerrain]->trigger(); });
        toolBar->addAction(QIcon("Resources/Icons/BiomeIcon.png"), "Biome", this, [&]() {actions[isKeyDown(VK_CONTROL) ? ELayoutAction::ExtractBiome : ELayoutAction::CreateBiome]->trigger(); });
        toolBar->addAction(QIcon("Resources/Icons/WaterIcon.png"), "Water", this, [&]() {actions[isKeyDown(VK_CONTROL) ? ELayoutAction::ExtractWater : ELayoutAction::CreateWater]->trigger(); });
        toolBar->addAction(toggleBrushAction);
        toolBar->addAction(satScanWindowAction);

        return toolBar;
    }

    QWidget* StageTools<EGenerationStage::Layout>::createPaintTool()
    {
        auto* paintTool = new QWidget();
        paintTool->setStyleSheet("QPushButton:checked {background-color: green ;}");

        auto* layout = new QGridLayout();
        paintTool->setLayout(layout);

        auto* domainGroup = new QButtonGroup(paintTool);
        auto* optionsGroup = new QButtonGroup(paintTool);

        // Domains
        auto makeButton = [&](const QString& label, auto lambda, int row, int col, bool startChecked = false)
        {
            auto* button = new QPushButton(QIcon(), label);
            button->setCheckable(true);
            button->setChecked(startChecked);
            connect(button, &QPushButton::toggled, this, lambda);

            if (col == 0)
                domainGroup->addButton(button);
            else
                optionsGroup->addButton(button);

            layout->addWidget(button, row, col);
        };

        makeButton("Paint Terrain", [&]() {domainTypeToPaint = EDomainType::Terrain; }, 0, 0, true);
        makeButton("Paint Biome", [&]() {domainTypeToPaint = EDomainType::Biome; }, 1, 0);
        makeButton("Paint Water", [&]() {domainTypeToPaint = EDomainType::Water; }, 2, 0);

        makeButton("Create", [&]() { paintingOption = EOmnigenPainting::New; }, 0, 1);
        makeButton("Append", [&]() { paintingOption = EOmnigenPainting::Append; }, 1, 1, true);
        makeButton("Extract", [&]() { paintingOption = EOmnigenPainting::Extract; }, 2, 1);
        makeButton("Subtract", [&]() { paintingOption = EOmnigenPainting::Subtract; }, 3, 1);

        return paintTool;
    }
}