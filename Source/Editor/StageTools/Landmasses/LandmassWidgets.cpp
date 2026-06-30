#include "stdafx.h"
#include "LandmassWidgets.h"
#include "Utils/PlatformMisc.h"
#include "Omnigen.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "LandmassSelection.h"
#include "StageToolsLandmasses.h"
#include "Editor/StageTools/StageTools.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"

#include <QToolBar>
#include "Editor/Sections/PropertySystem/Fields/ComboBoxField.h"

namespace Design
{
    LandmassTreeItem::LandmassTreeItem(const QSharedPointer<DLandmassMarker>& landmass, OutlineTreeItem* parent /*= nullptr*/)
        : OutlineTreeItem({ landmass->getName() }, parent)
        , guid(landmass->getGuid())
    {
    }

    LandmassTreeItem::LandmassTreeItem(const QSharedPointer<DShorelineMarker>& shoreline, OutlineTreeItem* parent /*= nullptr*/)
        : OutlineTreeItem({ shoreline->getName() }, parent)
        , guid(shoreline->getGuid())
    {
    }

    QVariant LandmassTreeItem::getDataByRole(int role) const
    {
        if (role == Guid)
            return guid;

        return {};
    }

    void QLandmassTreeModel::clearSelection()
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)) && !History::GetContext()->IsUndoingOrRedoing())
            return;

        if (treeView->selectionModel()->selection().isEmpty())
            return;

        treeView->selectionModel()->select(QModelIndex(), QItemSelectionModel::SelectionFlag::Clear);
    }

    void QLandmassTreeModel::selectItem(qint64 guid)
    {
        auto matches = match(index(0, 0), LandmassTreeItem::CustomRoles::Guid, guid, 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        QItemSelection s(match, match);

        s.merge(treeView->selectionModel()->selection(), QItemSelectionModel::SelectionFlag::Select);
        treeView->selectionModel()->select(s, QItemSelectionModel::SelectionFlag::Select);
    }

    void QLandmassTreeModel::addItem(size_t typeHash, QSharedPointer<Editable> object)
    {
        auto&& drawable = object.dynamicCast<OmnigenDrawable>();
        if (!drawable)
            return;

        // Ensure no duplicates
        auto matches = match(index(0, 0), LandmassTreeItem::CustomRoles::Guid, drawable->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (!matches.isEmpty())
            return;

        if (auto&& landmass = object.dynamicCast<DLandmassMarker>(); landmass)
            addLandmass(landmass);
        else if (auto&& shoreline = object.dynamicCast<DShorelineMarker>(); shoreline)
            addShoreline(shoreline);
    }

    void QLandmassTreeModel::addLandmass(QSharedPointer<DLandmassMarker> landmass)
    {
        beginResetModel();

        auto itemParent = getRootItem();

        auto newItem = new LandmassTreeItem(landmass, itemParent);
        itemParent->appendChild(newItem);

        endResetModel();
    }

    void QLandmassTreeModel::addShoreline(QSharedPointer<DShorelineMarker> shoreline)
    {
        Q_ASSERT(shoreline->getLandmass());

        beginResetModel();

        auto parentMatch = match(index(0, 0), LandmassTreeItem::CustomRoles::Guid, shoreline->getLandmass().lock()->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        auto itemParent = static_cast<LandmassTreeItem*>(parentMatch.front().internalPointer());

        auto newItem = new LandmassTreeItem(shoreline, itemParent);
        itemParent->appendChild(newItem);

        endResetModel();
    }

    void QLandmassTreeModel::removeItem(QSharedPointer<Editable> object)
    {
        auto&& drawable = object.dynamicCast<OmnigenDrawable>();
        if (!drawable)
            return;

        auto matches = match(index(0, 0), LandmassTreeItem::CustomRoles::Guid, drawable->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();
        auto* item = static_cast<OutlineTreeItem*>(matches.front().internalPointer());

        beginRemoveRows(match.parent(), match.row(), match.row());
        item->getParentItem()->removeChild(item);
        endRemoveRows();
    }

    void QLandmassTreeModel::updateItem(QSharedPointer<Editable> object, bool reset)
    {
        auto&& drawable = object.dynamicCast<OmnigenDrawable>();
        if (!drawable)
            return;

        auto matches = match(index(0, 0), LandmassTreeItem::CustomRoles::Guid, drawable->getGuid(), 1, Qt::MatchStartsWith | Qt::MatchWrap | Qt::MatchRecursive);
        if (matches.isEmpty())
            return;

        auto&& match = matches.front();

        auto* item = static_cast<LandmassTreeItem*>(match.internalPointer());

        if (auto&& landmass = object.dynamicCast<DLandmassMarker>(); landmass)
            item->setData(0, landmass->getName());
        else if (auto&& shoreline = object.dynamicCast<DShorelineMarker>(); shoreline)
            item->setData(0, shoreline->getName());

        emit dataChanged(match, match);
    }

    void QLandmassTreeModel::loadLandmasses()
    {
        beginResetModel();

        auto&& landmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();
        for (auto&& landmass : landmasses)
        {
            auto landmassItem = new LandmassTreeItem(landmass, getRootItem());
            getRootItem()->appendChild(landmassItem);

            for (auto&& shoreline : landmass->getShorelines())
            {
                auto shorelineItem = new LandmassTreeItem(shoreline, landmassItem);
                landmassItem->appendChild(shorelineItem);
            }

            for (auto&& shoreline : landmass->getInnerSeaShorelines())
            {
                auto shorelineItem = new LandmassTreeItem(shoreline, landmassItem);
                landmassItem->appendChild(shorelineItem);
            }
        }

        endResetModel();
    }

    void QLandmassTreeModel::clear()
    {
        beginResetModel();

        getRootItem()->clearChildren();

        endResetModel();
    }

    void QLandmassTreeModel::setTreeView(QTreeView* inView)
    {
        treeView = inView;

        connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &QLandmassTreeModel::treeSelectionChanged);
        connect(treeView, &QTreeView::doubleClicked, this, &QLandmassTreeModel::itemDoubleClicked);
        connect(treeView, &QTreeView::customContextMenuRequested, this, &QLandmassTreeModel::outlineContextMenuRequested);
    }

    void QLandmassTreeModel::treeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
    {
        if ((isKeyDown(VK_SHIFT) || isKeyDown(VK_CONTROL)))
            return;

        treeView->selectionModel()->blockSignals(true);

        LandmassSelectionMgr::get()->clearSelection();

        for (auto&& index : selected.indexes())
        {
            auto* item = static_cast<LandmassTreeItem*>(index.internalPointer());

            if (auto&& landmass = Generation::Data::get()->findMarkerByGuid<DLandmassMarker>(item->guid); landmass)
                LandmassSelectionMgr::get()->setSelection<Design::ELandmassSelection::Landmass>({ landmass });
            else if (auto&& shoreline = Generation::Data::get()->findMarkerByGuid<DShorelineMarker>(item->guid); shoreline)
                LandmassSelectionMgr::get()->setSelection<Design::ELandmassSelection::Shoreline>({ shoreline });
        }

        treeView->selectionModel()->blockSignals(false);
    }

    void QLandmassTreeModel::itemDoubleClicked(const QModelIndex& idx)
    {
        QOmnigenViewportSection::getActiveViewport()->tryMoveToSelection();
    }

    void QLandmassTreeModel::outlineContextMenuRequested(const QPoint& pos)
    {
        QModelIndex index = treeView->indexAt(pos);
        auto* item = static_cast<LandmassTreeItem*>(index.internalPointer());

        if (!item)
            return;

        if (auto&& landmass = Generation::Data::get()->findMarkerByGuid<DLandmassMarker>(item->guid); landmass)
        {
            QMenu* contextMenu = LandmassSelection::requestContextMenu(landmass);
            contextMenu->popup(treeView->viewport()->mapToGlobal(pos));
        }
        else if (auto&& shoreline = Generation::Data::get()->findMarkerByGuid<DShorelineMarker>(item->guid); shoreline)
        {
            QMenu* contextMenu = ShorelineSelection::requestContextMenu(shoreline);
            contextMenu->popup(treeView->viewport()->mapToGlobal(pos));
        }
    }

    QWidget* StageTools<EGenerationStage::Landmasses>::createOutlineToolbar()
    {
        auto* mainWidget = new QWidget();

        mainWidget->setContentsMargins(0, 0, 0, 0);
        auto* toolBar = new QToolBar();
        toolBar->setIconSize(QSize(40, 20));
        toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        mainWidget->setMaximumWidth(5000);

        auto* mainLayout = new QGridLayout(mainWidget);

        mainLayout->addWidget(toolBar, 0, 0, 1, -1);
        mainLayout->setContentsMargins(0, 0, 0, 0);

        auto* brushGroup = new QActionGroup(toolBar);
        brushGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

        auto* toggleLandmassSpawn = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Landmass Spawn", brushGroup);
        toggleLandmassSpawn->setCheckable(true);

        auto* toggleLandmassEdit = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Landmass Edit", brushGroup);
        toggleLandmassEdit->setCheckable(true);

        auto* toggleCliffEdit = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Cliff Edit", brushGroup);
        toggleCliffEdit->setCheckable(true);

        // Shoreline Spawn size
        auto* landmassSpawnSize = new ComboFieldEdit<ELandmassSize>(gatherEnumsForComboField<ELandmassSize>());
        spawnSize = ELandmassSize::Small;
        landmassSpawnSize->set(spawnSize);
        landmassSpawnSize->show();
        connect(landmassSpawnSize, &ComboFieldEdit<ELandmassSize>::valueChanged, this, [this, landmassSpawnSize]
            { spawnSize = landmassSpawnSize->get(); });

        auto* landmassSpawnSizeLabel = new QLabel("Spawn Size: ");

        landmassSpawnSize->setVisible(false);
        landmassSpawnSizeLabel->setVisible(false);

        // Shoreline Spawn complexity
        auto* landmassSpawnComplexity = new ComboFieldEdit<EShorelineComplexity>(gatherEnumsForComboField<EShorelineComplexity>());
        spawnComplexity = EShorelineComplexity::Medium;
        landmassSpawnComplexity->set(spawnComplexity);
        landmassSpawnComplexity->show();
        connect(landmassSpawnComplexity, &ComboFieldEdit<ELandmassSize>::valueChanged, this, [this, landmassSpawnComplexity]
            { spawnComplexity = landmassSpawnComplexity->get(); });

        auto* landmassSpawnComplexityLabel = new QLabel("Spawn Complexity: ");

        landmassSpawnComplexity->setVisible(false);
        landmassSpawnComplexityLabel->setVisible(false);

        // Cliff Editing Strength
        auto* cliffEditStrength = new QSlider(Qt::Orientation::Horizontal);
        cliffEditStrength->setMaximum(100);
        cliffEditStrength->setMinimum(1);
        cliffEditStrength->setValue(10);
        cliffStrength = 20;
        cliffEditStrength->show();
        auto* cliffEditStrengthLabel = new QLabel("Strength: " + QString::number(cliffStrength));
        connect(cliffEditStrength, &QSlider::valueChanged, this, [this, cliffEditStrength, cliffEditStrengthLabel]
            {
                cliffStrength = cliffEditStrength->value();
                cliffEditStrengthLabel->setText("Strength: " + QString::number(cliffStrength));
            });

        cliffEditStrength->setVisible(false);
        cliffEditStrengthLabel->setVisible(false);

        // Cliff Editing Use Flattening
        auto* cliffEditFlatteningToggle = new QCheckBox("Flatten points");
        cliffEditFlatteningToggle->setChecked(true);
        isCliffEditIncrementFlattening = true;
        cliffEditFlatteningToggle->setVisible(false);
        connect(cliffEditFlatteningToggle, &QCheckBox::stateChanged, this, [this, cliffEditFlatteningToggle]
            {isCliffEditIncrementFlattening = cliffEditFlatteningToggle->isChecked(); });

        // Shoreline Editing Brush Size Slider
        auto* landmassEditSize = new QSlider(Qt::Orientation::Horizontal);
        landmassEditSize->setMaximum(60);
        landmassEditSize->setMinimum(1);
        landmassEditSize->setValue(5);
        brushSize = 5;
        landmassEditSize->show();
        auto* landmassEditSizeLabel = new QLabel("Brush Size: " + QString::number(brushSize));
        connect(landmassEditSize, &QSlider::valueChanged, this, [this, landmassEditSize, landmassEditSizeLabel]
            {
                brushSize = landmassEditSize->value();
                landmassEditSizeLabel->setText("Brush Size: " + QString::number(brushSize));
            });

        landmassEditSize->setVisible(false);
        landmassEditSizeLabel->setVisible(false);

        mainLayout->addWidget(landmassSpawnSizeLabel, 2, 0);
        mainLayout->addWidget(landmassSpawnSize, 2, 1);
        mainLayout->addWidget(landmassSpawnComplexityLabel, 3, 0);
        mainLayout->addWidget(landmassSpawnComplexity, 3, 1);
        mainLayout->addWidget(landmassEditSizeLabel, 4, 0);
        mainLayout->addWidget(landmassEditSize, 4, 1);
        mainLayout->addWidget(cliffEditStrengthLabel, 5, 0);
        mainLayout->addWidget(cliffEditStrength, 5, 1);
        mainLayout->addWidget(cliffEditFlatteningToggle, 6, 0);
        mainLayout->setColumnMinimumWidth(2, 4700);

        // Edit Landmass
        auto* landmassEditToolbar = new QToolBar();
        landmassEditToolbar->setIconSize(QSize(40, 20));
        landmassEditToolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        mainLayout->addWidget(landmassEditToolbar, 1, 0, 1, -1);
        landmassEditToolbar->setVisible(false);

        auto* toolsGroup = new QActionGroup(landmassEditToolbar);
        toolsGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::Exclusive);

        // Append Tool
        auto* appendToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Append Tool", toolsGroup);
        appendToolButton->setCheckable(true);
        appendToolButton->setChecked(true);
        isLandmassEditAppend = true;
        isLandmassEditExtract = false;

        // Extract Tool
        auto* extractToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Extract Tool", toolsGroup);
        extractToolButton->setCheckable(true);

        connect(toolsGroup, &QActionGroup::triggered, this, [=]()
            {
                isLandmassEditAppend = appendToolButton->isChecked();
                isLandmassEditExtract = extractToolButton->isChecked();
            });

        landmassEditToolbar->addAction(appendToolButton);
        landmassEditToolbar->addAction(extractToolButton);
        
        // Edit Cliffs
        auto* cliffEditToolbar = new QToolBar();
        cliffEditToolbar->setIconSize(QSize(40, 20));
        cliffEditToolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        mainLayout->addWidget(cliffEditToolbar, 2, 0, 1, -1);
        cliffEditToolbar->setVisible(false);

        auto* cliffToolsGroup = new QActionGroup(cliffEditToolbar);
        cliffToolsGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

        // Increment Tool
        auto* incrementToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Increment Tool", cliffToolsGroup);
        incrementToolButton->setCheckable(true);
        incrementToolButton->setChecked(true);
        isCliffEditIncrement = true;

        // Flattening Tool
        auto* flatteningToolButton = new QAction(QIcon("Resources/Icons/BrushIcon.png"), "Flatten Tool", cliffToolsGroup);
        flatteningToolButton->setCheckable(true);

        connect(cliffToolsGroup, &QActionGroup::triggered, this, [=]()
            {
                isCliffEditIncrement = incrementToolButton->isChecked();
                isCliffEditFlattening = flatteningToolButton->isChecked();

                cliffEditFlatteningToggle->setVisible(toggleCliffEdit->isChecked() && incrementToolButton->isChecked());
            });

        cliffEditToolbar->addAction(incrementToolButton);
        cliffEditToolbar->addAction(flatteningToolButton);

        // Button logic
        connect(brushGroup, &QActionGroup::triggered, this, [=]()
            {
                if (toggleLandmassEdit->isChecked())
                    changeTo2DShorelines();
                else
                    changeTo3DShorelines();

                isLandmassSpawning = toggleLandmassSpawn->isChecked();
                isLandmassEditing = toggleLandmassEdit->isChecked();
                isCliffEditing = toggleCliffEdit->isChecked();

                landmassSpawnSize->setVisible(toggleLandmassSpawn->isChecked());
                landmassSpawnSizeLabel->setVisible(toggleLandmassSpawn->isChecked());
                landmassSpawnComplexity->setVisible(toggleLandmassSpawn->isChecked());
                landmassSpawnComplexityLabel->setVisible(toggleLandmassSpawn->isChecked());

                landmassEditSize->setVisible(toggleLandmassEdit->isChecked() || toggleCliffEdit->isChecked());
                landmassEditSizeLabel->setVisible(toggleLandmassEdit->isChecked() || toggleCliffEdit->isChecked());

                cliffEditStrength->setVisible(toggleCliffEdit->isChecked());
                cliffEditStrengthLabel->setVisible(toggleCliffEdit->isChecked());

                cliffEditFlatteningToggle->setVisible(toggleCliffEdit->isChecked() && incrementToolButton->isChecked());

                landmassEditToolbar->setVisible(toggleLandmassEdit->isChecked());
                cliffEditToolbar->setVisible(toggleCliffEdit->isChecked());
            });

        toolBar->addAction(toggleLandmassSpawn);
        toolBar->addAction(toggleLandmassEdit);
        toolBar->addAction(toggleCliffEdit);

        return mainWidget;
    }
}