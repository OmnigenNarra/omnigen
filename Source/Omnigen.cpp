#include "stdafx.h"
#include "Omnigen.h"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QShortcut>
#include <QTableView>
#include <QApplication>
#include <QAction>
#include <QWidgetAction>
#include <QMenuBar>
#include <QToolBar>
#include <QSplitter>
#include <QSettings>
#include <QCheckbox>
#include <QLabel>

#include "CodeSandbox.h"
#include "Editor/Framework/AdvancedDockingSystem/API.h"
#include "Editor/Framework/AdvancedDockingSystem/SectionContent.h"
#include "Utils/PlatformMisc.h"
#include "Editor/Sections/History/History.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainHandleDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainPaintingPreview.h"
#include "Scene/Generation/Stages/TerrainFinalization/TerrainChunkDrawable.h"
#include "Utils/CoreUtils.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include "Editor/Sections/Viewport/OmnigenViewportSection.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "Editor/Sections/OmniLog/OmnigenLogView/OmnigenLogSection.h"
#include "Editor/Sections/History/OmnigenHistoryStackSection.h"
#include "Editor/Sections/Outline/OmnigenOutlineSection.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Editor/Dialogs/Preferences/OmnigenPreferencesDialog.h"
#include "Editor/Dialogs/Preferences/OmnigenPreferences.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Scene/Generation/OmnigenGenerationData.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editor/Sections/OmniLog/OmnigenLogView/OmnigenLogMessageItemModel.h"
#include "Editor/Sections/Profiler/OmnigenProfilerSection.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/Sections/CameraSystem/OmnigenCameraSection.h"
#include "Editor/Framework/AdvancedDockingSystem/SectionWidget.h"

#include "Editor/OmniStageBar/OmniStageBar.h"
#include "Scene/Core/SkyboxDrawable.h"
#include "Scene/Generation/Stages/StageGeneration.h"

#include "Editor/StageTools/StageTools.h"
#include "Editor/StageTools/SelectionMgrBase.h"
#include "Utils/ManipulationGizmo.h"
#include "Source/Scene/Generation/Stages/TerrainMods/River/TerrainModRiverData.h"
#include "Source/Scene/Generation/Stages/Layout/Data/Biome/DomainData_Biome.h"

#include "Utils/Resumable.h"
#include "Data/Assets/Plant/AssetPlant.h"

#define SHOW_WELCOME_SCREEN 1

void Omnigen::action_generateAll()
{
    Generation::Data::get()->setGenerationStage(EGenerationStage(int(EGenerationStage::LastFunctional)), true, true);
}

void Omnigen::action_regenerateAll()
{
    Generation::Data::get()->setGenerationStage(EGenerationStage(0), true, false);
    action_generateAll();
}

QString Omnigen::sConfigName = "Config/omnigen.ini";
QString Omnigen::sGenerationConfigName = "Config/generation.ini";
QString Omnigen::sDefaultConfigName = "Config/default.ini";

const auto& toggleMap()
{
     static QMap<EOmnigenSection, EOmnigenAction> toggleMap;

     if (toggleMap.isEmpty())
         for (size_t i = 0; i < magic_enum::enum_count<EOmnigenSection>(); ++i)
             toggleMap[static_cast<EOmnigenSection>(i)] = static_cast<EOmnigenAction>(*magic_enum::enum_index(EOmnigenAction::ToggleViewport1) + i);

     return toggleMap;
}

Omnigen::Omnigen(QWidget* parent)
    : QFramelessWindow(parent)
{
    logMessages = QSharedPointer<OmnigenLogMessageItemModel>::create();

    setMinimumSize(800, 600);
    setFocusPolicy(Qt::ClickFocus);
}

void Omnigen::initialize()
{
    setupEvents();

    auto* mainContent = new QWidget();
    auto* mainLayout = new QVBoxLayout(mainContent);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* initialContent = createInitialContent();
    auto* menuBar = createMenuBar();
    auto* toolBar = createMainToolbar();
    stageBar = new QOmniStageBar(this);
    stageBar->setVisible(true);

    mainLayout->addWidget(menuBar);
    mainLayout->addWidget(toolBar);
    mainLayout->addWidget(stageBar);
    mainLayout->addWidget(mainContainer);

    setupShortcuts();

    setContent(mainContent);
    saveConfig(sDefaultConfigName);
    loadGenerationParams(sGenerationConfigName);

    loadConfig();
    updateToggleActionStates();

    clearAll();

    // Load assets from Resources/Assets
    getAssetsSection()->loadMetadata();

    if constexpr (SHOW_WELCOME_SCREEN)
        showWelcomeScreen();
    else
        setWindowTitle("Omnigen");
}

QOmnigenPropertiesSection* Omnigen::getProperties()
{
    return static_cast<QOmnigenPropertiesSection*>(sectionWidgets[EOmnigenSection::Properties]);
}

QOmnigenOutlineSection* Omnigen::getOutline()
{
    return static_cast<QOmnigenOutlineSection*>(sectionWidgets[EOmnigenSection::Outline]);
}

QOmnigenHistoryStackSection* Omnigen::getHistoryStack()
{
    return static_cast<QOmnigenHistoryStackSection*>(sectionWidgets[EOmnigenSection::HistoryStack]);
}

QOmnigenProfilerSection* Omnigen::getProfiler()
{
    return static_cast<QOmnigenProfilerSection*>(sectionWidgets[EOmnigenSection::Profiler]);
}

QOmnigenCameraSection* Omnigen::getCameraSection()
{
    return static_cast<QOmnigenCameraSection*>(sectionWidgets[EOmnigenSection::CameraSystem]);
}

QOmnigenAssetMgrSection* Omnigen::getAssetsSection()
{
    return static_cast<QOmnigenAssetMgrSection*>(sectionWidgets[EOmnigenSection::AssetMgr]);
}

const std::map<ELOD, float>& Omnigen::getDistanceMap() const
{
    // Max grid distance from camera to use that LOD.
    static const std::map<ELOD, float> distanceMap =
    {
        { ELOD::Zero, 50'00.0f },   // 50m
        { ELOD::Mid, 150'00.0f },   // 150m
        { ELOD::Far, 500'00.0f },   // 500m
        { ELOD::Last, FLT_MAX }
    };

    return distanceMap;
}

const QMap<int, QOmnigenViewport*>& Omnigen::getAllViewports() const
{
    return QOmnigenViewportSection::getAllViewports();
}

std::vector<QSet<GPoint>> Omnigen::partitionSquares(QSet<GPoint> squares)
{
    std::vector<QSet<GPoint>> result;
    QSet<GPoint> perimeter;
    QSet<GPoint> target;

    auto findNextSquare = [&perimeter, &squares]() -> std::optional<GPoint>
    {
        if (perimeter.isEmpty())
            return *squares.begin();

        for (auto&& sq : perimeter)
            if (squares.contains(sq))
                return sq;

        return {};
    };

    auto updatePerimeter = [&target, &perimeter](const GPoint& newSquare)
    {
        if (GPoint sq = { newSquare.x - 1, newSquare.z }; !target.contains(sq))
            perimeter += sq;

        if (GPoint sq = { newSquare.x + 1, newSquare.z }; !target.contains(sq))
            perimeter += sq;

        if (GPoint sq = { newSquare.x, newSquare.z - 1 }; !target.contains(sq))
            perimeter += sq;

        if (GPoint sq = { newSquare.x, newSquare.z + 1 }; !target.contains(sq))
            perimeter += sq;

        perimeter.remove(newSquare);
    };

    while (true)
    {
        if (squares.isEmpty())
            break;

        // Init new set
        target.clear();
        perimeter.clear();

        // Fill set by adjacency
        while (auto nextSquare = findNextSquare())
        {
            target += *nextSquare;
            squares.remove(*nextSquare);
            updatePerimeter(*nextSquare);
        }

        // Insert set into output
        result << target;
    }

    return result;
}

bool Omnigen::isSectionVisible(EOmnigenSection s) const
{
    auto&& section = sections[s];
    return section->containerWidget()->isSectionContentVisible(section);
}

bool Omnigen::isGenerating() const
{
    return bIsGenerating;
}

QDir Omnigen::getProjectDir()
{
    QString path = projectFileName.left(projectFileName.lastIndexOf('/'));
    return QDir(path);
}

void Omnigen::preClose()
{
    saveConfig();
}

void Omnigen::action_new()
{
    clearAll();

    projectFileName.clear();
    setWindowTitle("Untitled");

    action_save();
}

void Omnigen::action_save(bool isSaveAs)
{
    if (projectFileName.isEmpty() || isSaveAs)
    {
        QString newProjectFileName = QFileDialog::getSaveFileName(this, tr("Save Project"), "", tr("Omnigen project files (*.ogn)"));
        if (!newProjectFileName.isEmpty())
        {
            projectFileName = newProjectFileName;
        }
        else
        {
            if (projectFileName.isEmpty())
                showWelcomeScreen();

            return;
        }

        auto windowName = projectFileName.mid(projectFileName.lastIndexOf('/') + 1).remove(".ogn");
        setWindowTitle(windowName);
    }

    OmniLog(ELoggingLevel::Info) << "Saving " << projectFileName <<= "...";
    action_save_impl(projectFileName.toStdString());
    OmniLog(ELoggingLevel::Info) <<= "Save successful.";
}

void Omnigen::action_save_impl(const std::string& filename)
{
    OmniBin<std::ios::out> writer(filename);

    QOmnigenAssetMgrSection::syncDisabledAssets();
    writer << QOmnigenAssetMgrSection::getDisabledAssets();

    // Save cameras
    writer << *OmnigenCameraMgr::get();

    // Save at last stage with valid data
    EGenerationStage lastStageWithData = EGenerationStage::Last;
    EGenerationStage stageUserLeftOn = Generation::Data::get()->getGenerationStage();
    for (int stageIdx = int(EGenerationStage::LastFunctional); stageIdx >= 0; --stageIdx)
        if (lastStageWithData = EGenerationStage(stageIdx); EGenerationStageConstexpr::UseIn<EAC::HasDataToSave>(lastStageWithData))
            break;

    writer << lastStageWithData;
    writer << stageUserLeftOn;

    for (int i = 0; i <= int(lastStageWithData); ++i)
    {
        EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(EGenerationStage(i))->save(writer);
        writer << EGenerationStageConstexpr::UseIn<EAC::GetStageState>(EGenerationStage(i));
    }

    QOmnigenViewportSection::saveToggles(writer);
}

void Omnigen::action_open()
{
    QString newProjectFileName = QFileDialog::getOpenFileName(this, tr("Open Project"), "", tr("Omnigen project files (*.ogn)"));
    if (!newProjectFileName.isEmpty())
    {
        projectFileName = newProjectFileName;
    }
    else 
    {
        if (projectFileName.isEmpty())
            showWelcomeScreen();

        return;
    }

    auto windowName = projectFileName.mid(projectFileName.lastIndexOf('/') + 1).remove(".ogn");
    setWindowTitle(windowName);

    OmniLog(ELoggingLevel::Info) << "Opening " << projectFileName <<= "...";

    OmniBin<std::ios::in> reader(projectFileName.toStdString());

    std::unordered_set<qint64> disabledAssets;
    reader >> disabledAssets;
    for (qint64 id : disabledAssets)
        QOmnigenAssetMgrSection::setAssetEnabled(id, false);

    // Clear the scene
    clearAll();

    // Load cameras
    reader >> *OmnigenCameraMgr::get();

    EGenerationStage lastStageWithData;
    EGenerationStage stageUserLeftOn;
    reader >> lastStageWithData;
    reader >> stageUserLeftOn;

    {
        OmniStartProfiling;
        for (int i = 0; i <= int(lastStageWithData); ++i)
        {
            Generation::Data::get()->setCurrentGeneratedStage(EGenerationStage(i));
            EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(EGenerationStage(i))->load(reader);
            Generation::FStageStates stageState;
            reader >> stageState;
            EGenerationStageConstexpr::UseIn<EAC::SetStageState>(EGenerationStage(i), stageState);
            Generation::Data::get()->setCurrentGeneratedStage({});
        }
        Generation::Data::get()->initializeQueuedMarkers();
    }

    QOmnigenViewportSection::loadToggles(reader);

    Generation::Data::get()->setGenerationStage(stageUserLeftOn, false, false, true);

    QOmnigenViewportSection::repaintAll(true);
    getCameraSection()->restartItems();

    OmniLog(ELoggingLevel::Info) << "Successfully opened " <<= projectFileName;
}

void Omnigen::action_undo()
{
    if (!History::GetContext())
        return;

    if (auto* present = History::GetContext()->Present())
    {
        OmniLog(ELoggingLevel::Info) << "Begin undo " <<= present->GetLabel();
        History::GetContext()->Undo();
        OmniLog(ELoggingLevel::Info) <<= "Undo successful!";
    }
    else
    {
        OmniLog(ELoggingLevel::Warn) <<= "Nothing left to undo.";
    }
}

void Omnigen::action_redo()
{
    if (!History::GetContext())
        return;

    if (auto* future = History::GetContext()->PeekFuture())
    {
        OmniLog(ELoggingLevel::Info) << "Begin redo " <<= future->GetLabel();
        History::GetContext()->Redo();
        OmniLog(ELoggingLevel::Info) <<= "Redo successful!";
    }
    else
    {
        OmniLog(ELoggingLevel::Warn) <<= "Nothing left to redo.";
    }
}

void Omnigen::action_cut()
{
    OmniLog() <<= "Cut!";
}

void Omnigen::action_copy()
{
    OmniLog() <<= "Copy!";
}

void Omnigen::action_paste()
{
    OmniLog() <<= "Paste!";
}

void Omnigen::action_resetViewport()
{
    OmniLog(ELoggingLevel::Info) <<= "Begin reset viewports";

    for (auto&& vp : getAllViewports())
        vp->resetView();

    QOmnigenViewportSection::changeActiveViewport(getAllViewports().first());

    OmniLog(ELoggingLevel::Info) <<= "Viewports reset successfully.";
}

void Omnigen::action_preferences()
{
    QOmnigenPreferencesDialog preferences;
    preferences.exec();
}

void Omnigen::action_resetWindowLayout()
{
    OmniLog(ELoggingLevel::Info) <<= "Begin reset window layout";

    for (auto&& sc : mainContainer->contents())
        mainContainer->showSectionContent(sc);

    loadConfig(sDefaultConfigName);
    updateToggleActionStates();

    QTimer::singleShot(0, this, [this]() { updateAll(this); });

    OmniLog(ELoggingLevel::Info) <<= "Layout reset successfully";
}

static std::vector<GVector2D> makeDetailedPolygon(const std::vector<GVector2D>& inPolygon)
{
    auto cPts = asCircular(inPolygon);

    std::vector<GVector2D> result;
    for (int i = 0; i < cPts.getSize(); ++i)
    {
        auto detailedSegment = splitSegment(Segment2D(cPts[i], cPts[cPts.findIdx(i, 1)]), FFirstLastPolicy::First, true);
        for (auto&& p : detailedSegment)
            result.push_back(p);
    }

    return result;
}

#include "Scene/Generation/Common/Markers/BatchingLineMarker.h"
void Omnigen::action_about()
{
    std::vector<GVector2D> pts =
    {
        //{296246,278416},{296273,278409},{296298,278402},{296323,278395},{296344,278388},{296361,278379},{296373,278372},{296382,278364},{296389,278356},{296395,278347},{296400,278339},{296404,278331},{296409,278323},{296413,278315},{296418,278306},{296423,278297},{296429,278288},{296435,278280},{296441,278271},{296448,278264},{296455,278257},{296462,278250},{296469,278243},{296476,278237},{296483,278230},{296490,278224},{296497,278218},{296504,278212},{296511,278206},{296518,278200},{296525,278194},{296532,278188},{296539,278182},{296546,278175},{296552,278170},{296557,278166},{296560,278164},{296560,278163},{296559,278164},{296560,278163},{296566,278156},{296586,278135},{296631,278083},{296583,278000},{296487,278000},{296391,278000},{296295,278000},{296247,278083},{296198,278166},{296150,278249},{296198,278333}
        {178300,191813},{178267,191726},{178234,191639},{178232,191639},{178233,191640},{178234,191643},{178235,191647},{178237,191653},{178239,191660},{178241,191667},{178244,191675},{178246,191684},{178248,191692},{178250,191702},{178253,191712},{178255,191722},{178257,191732},{178260,191742},{178262,191752},{178264,191761},{178266,191770},{178268,191779},{178271,191787},{178273,191796},{178275,191805},{178277,191813},{178278,191822},{178280,191831},{178281,191841}
    };

    Polygon2D poly(pts, true);
    //poly.debugPlot(Colors::green, 20.0f);

    auto [geom2D, unusedOuters] = meshPolygon2(poly.getPts());

    GeometryData<QVector3D> geom3D;
    geom3D.indices = std::move(geom2D.indices);
    geom3D.vertices = { geom2D.vertices.begin(), geom2D.vertices.end() };

    spawnBatched(std::move(geom3D), MeshBatchParams<QVector3D>{.color = Colors::cyan, .primitiveType = GL_TRIANGLES});
    Generation::Data::get()->initializeQueuedMarkers();
}

void Omnigen::action_resume()
{
    resumable::Awaiter::resume();
    Generation::Data::get()->initializeQueuedMarkers();
}

EGenerationStage Omnigen::action_generate(EGenerationStage targetStage)
{
    // Init
    QApplication::setOverrideCursor(Qt::WaitCursor);

    OmniLog(ELoggingLevel::Info) <<= "Generation started.";
    bIsGenerating = true;
    float generationTime = 0.0f;

    // Generation
    generationTime = timeFromLastEntrance<std::chrono::seconds>(888);
    EGenerationStage lastValid = Generation::processStages(Generation::Data::get()->getGenerationStage(), targetStage);
    generationTime = timeFromLastEntrance<std::chrono::seconds>(888);

    // Cleanup
    bIsGenerating = false;
    QOmnigenViewportSection::repaintAll();

    if (lastValid == targetStage)
        OmniLog(ELoggingLevel::Info) << "Generation successful! Time: " << generationTime <<= "s";

    QApplication::restoreOverrideCursor();
    return lastValid;
}

QWidget* Omnigen::createMenuBar()
{
    auto* menuBar = new QMenuBar();

    // File menu
    auto* fileMenu = new QMenu("File");

    actions[EOmnigenAction::New] = new QAction(QIcon(), tr("&New..."), this);
    QAction* newAction = actions[EOmnigenAction::New];
    newAction->setShortcuts(QKeySequence::New);
    newAction->setStatusTip(tr("Create a new world"));
    connect(newAction, &QAction::triggered, this, &Omnigen::action_new);
    fileMenu->addAction(newAction);

    actions[EOmnigenAction::Open] = new QAction(QIcon(), tr("&Open..."), this);
    QAction* openAction = actions[EOmnigenAction::Open];
    openAction->setShortcuts(QKeySequence::Open);
    openAction->setStatusTip(tr("Open an existing world"));
    connect(openAction, &QAction::triggered, this, &Omnigen::action_open);
    fileMenu->addAction(openAction);

    actions[EOmnigenAction::Save] = new QAction(QIcon(), tr("&Save"), this);
    QAction* saveAction = actions[EOmnigenAction::Save];
    saveAction->setShortcuts(QKeySequence::Save);
    saveAction->setStatusTip(tr("Save current world"));
    connect(saveAction, &QAction::triggered, this, &Omnigen::action_save);
    fileMenu->addAction(saveAction);

    actions[EOmnigenAction::SaveAs] = new QAction(QIcon(), tr("&Save As"), this);
    QAction* saveAsAction = actions[EOmnigenAction::SaveAs];
    saveAsAction->setShortcut(QKeySequence(tr("CTRL+SHIFT+S")));
    saveAsAction->setStatusTip(tr("Save current world with selected name"));
    connect(saveAsAction, &QAction::triggered, this, [this]() {action_save(true); });
    fileMenu->addAction(saveAsAction);

    menuBar->addMenu(fileMenu);

    // Edit menu
    auto* editMenu = new QMenu("Edit");

    actions[EOmnigenAction::Undo] = new QAction(QIcon(), tr("Undo"), this);
    QAction* undoAction = actions[EOmnigenAction::Undo];
    undoAction->setStatusTip(tr("Undoes last action"));
    connect(undoAction, &QAction::triggered, this, &Omnigen::action_undo);
    editMenu->addAction(undoAction);

    actions[EOmnigenAction::Redo] = new QAction(QIcon(), tr("Redo"), this);
    QAction* redoAction = actions[EOmnigenAction::Redo];
    redoAction->setStatusTip(tr("Redoes last undone action"));
    connect(redoAction, &QAction::triggered, this, &Omnigen::action_redo);
    editMenu->addAction(redoAction);

    editMenu->addSeparator();

    actions[EOmnigenAction::Cut] = new QAction(QIcon(), tr("Cut"), this);
    QAction* cutAction = actions[EOmnigenAction::Cut];
    cutAction->setShortcuts(QKeySequence::Cut);
    cutAction->setStatusTip(tr("Copy and remove selection"));
    connect(cutAction, &QAction::triggered, this, &Omnigen::action_cut);
    editMenu->addAction(cutAction);

    actions[EOmnigenAction::Copy] = new QAction(QIcon(), tr("Copy"), this);
    QAction* copyAction = actions[EOmnigenAction::Copy];
    copyAction->setShortcuts(QKeySequence::Copy);
    copyAction->setStatusTip(tr("Copy selection"));
    connect(copyAction, &QAction::triggered, this, &Omnigen::action_copy);
    editMenu->addAction(copyAction);

    actions[EOmnigenAction::Paste] = new QAction(QIcon(), tr("Paste"), this);
    QAction* pasteAction = actions[EOmnigenAction::Paste];
    pasteAction->setShortcuts(QKeySequence::Paste);
    pasteAction->setStatusTip(tr("Paste contents from clipboard"));
    connect(pasteAction, &QAction::triggered, this, &Omnigen::action_paste);
    editMenu->addAction(pasteAction);

    menuBar->addMenu(editMenu);

    // View menu
    auto* viewMenu = new QMenu("View");

    actions[EOmnigenAction::ResetView] = new QAction(QIcon(), tr("Reset Viewport"), this);
    QAction* resetViewportAction = actions[EOmnigenAction::ResetView];
    resetViewportAction->setStatusTip(tr("Resets camera position to default"));
    connect(resetViewportAction, &QAction::triggered, this, &Omnigen::action_resetViewport);
    viewMenu->addAction(resetViewportAction);

    actions[EOmnigenAction::Preferences] = new QAction(QIcon(), tr("Preferences"), this);
    QAction* preferencesAction = actions[EOmnigenAction::Preferences];
    preferencesAction->setStatusTip(tr("Show preferences dialog"));
    connect(preferencesAction, &QAction::triggered, this, &Omnigen::action_preferences);
    viewMenu->addAction(preferencesAction);

    menuBar->addMenu(viewMenu);

    // Window menu
    auto* windowMenu = new QMenu("Window");

    // Section toggles
    auto createSectionToggleAction = [this, windowMenu](EOmnigenSection s)
    {
        QString name = toQString(s);
        EOmnigenAction a = toggleMap()[s];
        actions[a] = new QAction(QIcon(), name, this);
        QAction* action = actions[a];
        action->setStatusTip(QString("Show/Hide ") + name);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, s]() { toggleSectionVisibility(s); });
        connect(mainContainer, &ContainerWidget::sectionContentVisibilityChanged, [this, action, s](auto&& sc, bool visible) { if (sc == sections[s]) action->setChecked(visible); });
        windowMenu->addAction(action);
    };

    createSectionToggleAction(EOmnigenSection::Viewport1);
    createSectionToggleAction(EOmnigenSection::Viewport2);
    createSectionToggleAction(EOmnigenSection::Viewport3);
    createSectionToggleAction(EOmnigenSection::Viewport4);
    createSectionToggleAction(EOmnigenSection::Outline);
    createSectionToggleAction(EOmnigenSection::Properties);
    createSectionToggleAction(EOmnigenSection::Log);
    createSectionToggleAction(EOmnigenSection::HistoryStack);
    createSectionToggleAction(EOmnigenSection::Profiler);
    createSectionToggleAction(EOmnigenSection::CameraSystem);
    createSectionToggleAction(EOmnigenSection::AssetMgr);

    actions[EOmnigenAction::ResetLayout] = new QAction(QIcon(), tr("Reset Window Layout"), this);
    QAction* resetWindowLayoutAction = actions[EOmnigenAction::ResetLayout];
    resetWindowLayoutAction->setStatusTip(tr("Resets all windows to default setup"));
    connect(resetWindowLayoutAction, &QAction::triggered, this, &Omnigen::action_resetWindowLayout);
    windowMenu->addAction(resetWindowLayoutAction);

    menuBar->addMenu(windowMenu);

    // Prevent last viewport from being closed
    connect(mainContainer, &ContainerWidget::sectionContentVisibilityChanged, this, [this](auto&& sc, bool visible)
        {
            if (sc != sections[EOmnigenSection::Viewport1] && sc != sections[EOmnigenSection::Viewport2]
                && sc != sections[EOmnigenSection::Viewport3] && sc != sections[EOmnigenSection::Viewport4])
                return;

            std::vector<EOmnigenSection> viewports = { EOmnigenSection::Viewport1, EOmnigenSection::Viewport2, EOmnigenSection::Viewport3, EOmnigenSection::Viewport4 };
            int avp = 0;
            EOmnigenSection activeVp;
            for (auto&& s : viewports)
            {
                if (isSectionVisible(s))
                {
                    ++avp;
                    activeVp = s;
                }
            }

            if (avp == 1)
            {
                lastViewportWindow = actions[toggleMap().value(activeVp)];
                lastViewportWindow->setDisabled(true);
                findParentSectionWidget(sections[activeVp]->contentWidget())->toggleCloseButtonVisibility(false);
            }
            else if (lastViewportWindow != nullptr)
            {
                lastViewportWindow->setDisabled(false);
                lastViewportWindow = nullptr;
                findParentSectionWidget(sections[activeVp]->contentWidget())->toggleCloseButtonVisibility(true);
            }
        });

    // Help menu
    auto* helpMenu = new QMenu("Help");
    
    actions[EOmnigenAction::About] = new QAction(QIcon(), tr("About Omnigen"), this);
    QAction* aboutAction = actions[EOmnigenAction::About];
    connect(aboutAction, &QAction::triggered, this, &Omnigen::action_about);
    helpMenu->addAction(aboutAction);

    actions[EOmnigenAction::DeveloperResume] = new QAction(QIcon(), tr("Resume last action"), this);
    QAction* resumeAction = actions[EOmnigenAction::DeveloperResume];
    resumeAction->setShortcut(QKeySequence(tr("CTRL+R")));
    connect(resumeAction, &QAction::triggered, this, &Omnigen::action_resume);
    helpMenu->addAction(resumeAction);
    
    menuBar->addMenu(helpMenu);

    return menuBar;
}

QWidget* Omnigen::createMainToolbar()
{
    auto* toolBar = new QToolBar();
    toolBar->setIconSize(QSize(40, 20));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolBar->setMinimumHeight(60);

    actions[EOmnigenAction::Generate] = new QAction(QIcon("Resources/Icons/GenerateIcon.png"), tr("Generate"), this);
    connect(actions[EOmnigenAction::Generate], &QAction::triggered, this, &Omnigen::action_generateAll);

    actions[EOmnigenAction::Regenerate] = new QAction(QIcon("Resources/Icons/GenerateIcon.png"), tr("Regenerate All"), this);
    connect(actions[EOmnigenAction::Regenerate], &QAction::triggered, this, &Omnigen::action_regenerateAll);

    actions[EOmnigenAction::ToggleStageBar] = new QAction(QIcon("Resources/Icons/NewIcon.png"), QString("Current Stage: ") + toQString(EGenerationStage(0)), this);
    actions[EOmnigenAction::ToggleStageBar]->setCheckable(true);
    actions[EOmnigenAction::ToggleStageBar]->setChecked(true);
    connect(actions[EOmnigenAction::ToggleStageBar], &QAction::triggered, this, [this]() 
        {
            actions[EOmnigenAction::ToggleStageBar]->setChecked(!stageBar->isVisible());
            stageBar->setVisible(!stageBar->isVisible());
        });

    auto* clearStageAction = new QAction(QIcon(), "Clear Stage", this);
    connect(clearStageAction, &QAction::triggered, this, [this]()
        {
            auto stage = Generation::Data::get()->getGenerationStage();

            EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(stage)->clearHistory();
            EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(stage)->unbind();
            
            EGenerationStageConstexpr::UseIn<EAC::ClearStage>(stage);
            EGenerationStageConstexpr::UseIn<EAC::InitializeStage>(stage);

            Generation::Data::get()->clearDebugMarkers();
            QOmnigenViewportSection::repaintAll();

            EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(stage)->bind();
        });

    auto* generateStageAction = new QAction(QIcon(), "Generate Stage", this);
    connect(generateStageAction, &QAction::triggered, this, [this]()
        {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            OmniStartProfiling;

            OmniLog(ELoggingLevel::Info) <<= "Generation started.";
            bIsGenerating = true;
            float generationTime = 0.0f;

            auto stage = Generation::Data::get()->getGenerationStage();
            EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(stage)->unbind();

            // Generation
            generationTime = timeFromLastEntrance<std::chrono::seconds>(888);
            EGenerationStageConstexpr::UseIn<EAC::AutoGenStage>(stage);
            generationTime = timeFromLastEntrance<std::chrono::seconds>(888);

            OmniLog(ELoggingLevel::Info) << "Generation complete. Time: " << generationTime <<= "s";

            // Cleanup
            Generation::Data::get()->initializeQueuedMarkers();
            bIsGenerating = false;
            QOmnigenViewportSection::repaintAll();
            EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(stage)->bind();

            Generation::getEventMgr().TriggerEvent<Generation::EGenerationEvent::Generated>();

            QApplication::restoreOverrideCursor();
        });

    auto* resetToStageAction = new QAction(QIcon(), "Reset to this stage", this);
    connect(resetToStageAction, &QAction::triggered, this, [this]()
        {
            auto currentStage = Generation::Data::get()->getGenerationStage();
            for (int stageIdx = int(EGenerationStage::LastFunctional); stageIdx > int(currentStage); --stageIdx)
            {
                EGenerationStageConstexpr::UseIn<EAC::ClearStage>(EGenerationStage(stageIdx));
                EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(EGenerationStage(stageIdx))->clearNodes();
            }
            EGenerationStageConstexpr::UseIn<EAC::InvalidateStage>(EGenerationStage(currentStage));

            Generation::Data::get()->clearDebugMarkers();
        });

    toolBar->addAction(QIcon("Resources/Icons/NewIcon.png"), "New", actions[EOmnigenAction::New], &QAction::triggered);
    toolBar->addAction(QIcon("Resources/Icons/LoadIcon.png"), "Open", actions[EOmnigenAction::Open], &QAction::triggered);
    toolBar->addAction(QIcon("Resources/Icons/SaveIcon.png"), "Save", actions[EOmnigenAction::Save], &QAction::triggered);
    toolBar->addAction(QIcon("Resources/Icons/SaveIcon.png"), "Save As", actions[EOmnigenAction::SaveAs], &QAction::triggered);
    toolBar->addAction(actions[EOmnigenAction::Generate]);
    toolBar->addAction(actions[EOmnigenAction::Regenerate]);
    toolBar->addAction(actions[EOmnigenAction::ToggleStageBar]);
    toolBar->addAction(generateStageAction);
    toolBar->addAction(clearStageAction);
    toolBar->addAction(resetToStageAction);

    return toolBar;
}

QWidget* Omnigen::createInitialContent()
{
    mainContainer = new ContainerWidget();

    //OmniLogger::get().setProgramOutputModel(&logMessages);
    OmniLogger::get().initProgramOutput(logMessages.get());

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Default tabs
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    auto createViewportSection = [&](EOmnigenSection num)
    {
        auto* viewportSectionContent = beginOmnigenSection();
        sectionWidgets[num] = new QOmnigenViewportSection(viewportSectionContent, num);
        finalizeOmnigenSection("Viewport #" + toQString(int(num) + 1), num, nullptr, (int(num) == 0) ? CenterDropArea : InvalidDropArea);
    };

    createViewportSection(EOmnigenSection::Viewport1);
    createViewportSection(EOmnigenSection::Viewport2);
    createViewportSection(EOmnigenSection::Viewport3);
    createViewportSection(EOmnigenSection::Viewport4);

    // Setup data.
    QTimer::singleShot(0, this, [this]() 
        { 
            grid = QSharedPointer<DEditorGrid>::create();
            grid->initialize();
            QOmnigenViewport::registerDrawable(grid);

            skybox = QSharedPointer<DSkybox>::create();
            skybox->initialize();
            QOmnigenViewport::registerDrawable(skybox);

            gizmo = static_cast<QSharedPointer<DManipulationGizmo>>(DManipulationGizmo::get());
            gizmo->initialize();
            QOmnigenViewport::registerDrawable(gizmo);
        });

    // Outline tab
    auto* outlineSectionContent = beginOmnigenSection();
    sectionWidgets[EOmnigenSection::Outline] = new QOmnigenOutlineSection(outlineSectionContent, this);
    finalizeOmnigenSection("Outline", EOmnigenSection::Outline, nullptr, RightDropArea);

    // Properties tab
    auto* propertiesSectionContent = beginOmnigenSection();
    sectionWidgets[EOmnigenSection::Properties] = new QOmnigenPropertiesSection(propertiesSectionContent);
    finalizeOmnigenSection("Properties", EOmnigenSection::Properties, findParentSectionWidget(sections[EOmnigenSection::Outline]->contentWidget()), BottomDropArea);

    // Setup proportions
    QSplitter* viewport2tools = findParentSplitter(sections[EOmnigenSection::Viewport1]->contentWidget());
    viewport2tools->setSizes({ int(size().width() * 0.8), int(size().width() * 0.2) });

    QSplitter* outline2properties = findParentSplitter(sections[EOmnigenSection::Outline]->contentWidget());
    outline2properties->setSizes({ int(size().height() * 0.6), int(size().height() * 0.4) });

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Hidden tabs
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Log tab
    auto* logWindowSectionContent = beginOmnigenSection();
    auto* logWindowLayout = new QVBoxLayout(logWindowSectionContent);
    auto* logWindowInnards = new OmnigenLogSection(logWindowSectionContent, logMessages.get());
    logWindowLayout->addWidget(logWindowInnards);
    logWindowInnards->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
    sectionWidgets[EOmnigenSection::Log] = logWindowInnards;
    finalizeOmnigenSection("Log", EOmnigenSection::Log, nullptr, InvalidDropArea);

    connect(mainContainer, &ContainerWidget::sectionContentVisibilityChanged, logWindowInnards, [=](auto&& sc, bool visible) { if (sc == sections[EOmnigenSection::Log]) { logWindowInnards->toggleTableUpdate(visible); } });

    // History Stack tab
    auto* historySectionContent = beginOmnigenSection();
    sectionWidgets[EOmnigenSection::HistoryStack] = new QOmnigenHistoryStackSection(historySectionContent);
    finalizeOmnigenSection("History Stack", EOmnigenSection::HistoryStack, nullptr, InvalidDropArea);

    // Profiler tab
    auto* profilerSectionContent = beginOmnigenSection();
    sectionWidgets[EOmnigenSection::Profiler] = new QOmnigenProfilerSection(profilerSectionContent);
    finalizeOmnigenSection("Profiler", EOmnigenSection::Profiler, nullptr, InvalidDropArea);

    // Camera System tab
    auto* cameraSystemSectionContent = beginOmnigenSection();
    sectionWidgets[EOmnigenSection::CameraSystem] = new QOmnigenCameraSection(cameraSystemSectionContent);
    finalizeOmnigenSection("Camera System", EOmnigenSection::CameraSystem, nullptr, InvalidDropArea);

    // Assets Mgr tab
    auto* assetMgrSectionContent = beginOmnigenSection();
    sectionWidgets[EOmnigenSection::AssetMgr] = new QOmnigenAssetMgrSection(assetMgrSectionContent);
    finalizeOmnigenSection("Assets", EOmnigenSection::AssetMgr, nullptr, InvalidDropArea);

    return mainContainer;
}

void Omnigen::clearAll()
{
    // Clear everything
    for (int stageIdx = int(EGenerationStage::LastFunctional); stageIdx >= 0; --stageIdx)
    {
        auto stage = EGenerationStage(stageIdx);
        EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(stage)->clearHistory();
        EGenerationStageConstexpr::UseIn<EAC::ClearStage>(stage);
        EGenerationStageConstexpr::UseIn<EAC::GetStageTools>(stage)->clearNodes();
    }

    // Set stage to layout
    Generation::Data::get()->setGenerationStage(EGenerationStage::Layout, false, false);

    // Cameras
    auto* cm = OmnigenCameraMgr::get(true);
    for (auto&& cam : cm->getAllActiveCameras())
        cm->setThumbnailForCamera(cam);

    getCameraSection()->restartItems();
    QOmnigenViewportSection::repaintAll(true);
}

void Omnigen::setupEvents()
{
    Editable::initEvents();

    // Main window
    connect(this, &QFramelessWindow::BEGIN_CLOSE, this, &Omnigen::preClose);

    // Omnigen
    auto connectForever1 = Editable::EventMgr.AddEventListener<EEditableEvents::Created>(this, &Omnigen::registerDrawable);
    auto connectForever2 = Editable::EventMgr.AddEventListener<EEditableEvents::AboutToBeDeleted>(this, &Omnigen::unregisterDrawable);

    // Init generation events
    Generation::getEventMgr().DeclareEvent<Generation::EGenerationEvent::Generated>();
}

void Omnigen::toggleSectionVisibility(EOmnigenSection s)
{
    auto&& section = sections[s];
    if (section->containerWidget()->isSectionContentVisible(section))
        section->containerWidget()->hideSectionContent(section);
    else
        section->containerWidget()->raiseSectionContent(section);
}

void Omnigen::updateToggleActionStates()
{
    // Update toggle action states.
    if (magic_enum::enum_count<EOmnigenSection>() != (*magic_enum::enum_index(EOmnigenAction::EndToggles) - *magic_enum::enum_index(EOmnigenAction::ToggleViewport1)))
        Q_ASSERT_X(false, "Actions", "Sections -> Section Toggles mismatch");

    for (auto&& s2a = toggleMap().keyValueBegin(); s2a != toggleMap().keyValueEnd(); ++s2a)
    {
        if (!actions.contains((*s2a).second))
            Q_ASSERT_X(false, "Actions", "Action not initialized!");
        if (!sections.contains((*s2a).first))
            Q_ASSERT_X(false, "Actions", "Section not initialized!");

        auto action = actions[(*s2a).second];
        auto section = sections[(*s2a).first];
        QString actionStr = toQString((*s2a).second);
        QString sectionStr = toQString((*s2a).first);
        if (!actionStr.contains(sectionStr))
            Q_ASSERT_X(false, "Actions", "Section & Section toggle action order mismatch!");

        action->setChecked(section->containerWidget()->isSectionContentVisible(section));
    }
}

void Omnigen::setupShortcuts()
{
    // Undo/Redo
    undoShortcut = new QShortcut(this);
    undoShortcut->setKey(QKeySequence::Undo);
    connect(undoShortcut, &QShortcut::activated, this, &Omnigen::action_undo);

    redoShortcut = new QShortcut(this);
    redoShortcut->setKey(QKeySequence::Redo);
    connect(redoShortcut, &QShortcut::activated, this, &Omnigen::action_redo);

    generateAllShortcut = new QShortcut(this);
    generateAllShortcut->setKey(Qt::Key_F5);
    connect(generateAllShortcut, &QShortcut::activated, this, &Omnigen::action_generateAll);

    generateStepBackShortcut = new QShortcut(this);
    generateStepBackShortcut->setKey(Qt::Key_F6);
    connect(generateStepBackShortcut, &QShortcut::activated, this, [this]()
        {
            auto stage = Generation::Data::get()->getGenerationStage();
            if (int(stage) > 0)
                Generation::Data::get()->setGenerationStage(EGenerationStage(int(stage) - 1), true, true);
        });

    generateStepForwardShortcut = new QShortcut(this);
    generateStepForwardShortcut->setKey(Qt::Key_F7);
    connect(generateStepForwardShortcut, &QShortcut::activated, this, [this]()
        {
            auto stage = Generation::Data::get()->getGenerationStage();
            if (stage < EGenerationStage::LastFunctional)
                Generation::Data::get()->setGenerationStage(EGenerationStage(int(stage) + 1), true, true);
        });

    regenerateCurrentStageShortcut = new QShortcut(this);
    regenerateCurrentStageShortcut->setKey(Qt::Key_F8);
    connect(regenerateCurrentStageShortcut, &QShortcut::activated, this, [this]()
        {
            Generation::Data::get()->setGenerationStage(Generation::Data::get()->getGenerationStage(), true, true);
        });

    regenerateAllShortcut = new QShortcut(this);
    regenerateAllShortcut->setKey(Qt::Key_F9);
    connect(regenerateAllShortcut, &QShortcut::activated, this, &Omnigen::action_regenerateAll);

    // Disable shortcuts
    connect(this, &Omnigen::toggleShortcut, this, [this](auto&& enable)
        {
            undoShortcut->setEnabled(enable);
            redoShortcut->setEnabled(enable);

            generateAllShortcut->setEnabled(enable);
            generateStepForwardShortcut->setEnabled(enable);
            generateStepBackShortcut->setEnabled(enable);
            regenerateCurrentStageShortcut->setEnabled(enable);
            regenerateAllShortcut->setEnabled(enable);
        });
}

void Omnigen::showWelcomeScreen()
{
    QMessageBox msgBox;
    msgBox.setText("WELCOME!");
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Open);
    msgBox.setDefaultButton(QMessageBox::Save);

    int ret = msgBox.exec();
    switch (ret)
    {
        case QMessageBox::Save: action_save(); break;
        case QMessageBox::Open: action_open(); break;
    }
}

void Omnigen::registerDrawable(size_t typeHash, QSharedPointer<Editable> e)
{
    if (auto&& drawable = e.dynamicCast<OmnigenDrawable>(); drawable)
        QOmnigenViewport::registerDrawable(drawable);
}

void Omnigen::unregisterDrawable(QSharedPointer<Editable> e)
{
    if (auto&& drawable = e.dynamicCast<OmnigenDrawable>(); drawable)
        QOmnigenViewport::unregisterDrawable(drawable);
}

void Omnigen::autoSave(EGenerationStage currentStage)
{
    if (!autosaveEndStage || int(currentStage) > int(*autosaveEndStage))
        return;

    QString filename = "Output/AutoSave";
    QDir directory;
    if (!directory.exists(filename))
        directory.mkpath(filename);

    filename += "/tmp.ogn";
    action_save_impl(filename.toStdString());
}

void Omnigen::saveConfig(const QString& filename)
{
    QSettings settings(filename, QSettings::Format::IniFormat);
    settings.setValue("editorLayout", mainContainer->saveState());

    // Save preferences
    OmnigenPreferences::get()->save(settings);
}

void Omnigen::loadConfig(const QString& filename)
{
    QSettings settings(filename, QSettings::Format::IniFormat);
    if (!settings.contains("editorLayout"))
        return;

    if (!settings.contains("autosaveEndStage"))
        settings.setValue("autosaveEndStage", QString::fromStdString(std::string(magic_enum::enum_name<EGenerationStage>(EGenerationStage::FeaturePlacement))));

    QByteArray savedLayout = settings.value("editorLayout").toByteArray();
    mainContainer->restoreState(savedLayout);

    // Auto Save
    autosaveEndStage = magic_enum::enum_cast<EGenerationStage>(settings.value("autosaveEndStage").toString().toStdString());

    // Load preferences
    OmnigenPreferences::get()->load(settings);
}

void Omnigen::loadGenerationParams(const QString& filename)
{
    QSettings settings(filename, QSettings::Format::IniFormat);

#define SET_VALUE(Object, Variable) setValue(Object##.##Variable, #Variable) 
    auto setValue = [&settings]<typename T>(T& param, const QString& key)
    {
        Q_ASSERT(settings.contains(key));

        if constexpr (std::is_same_v<T,int>)
        {
            param = settings.value(key).toInt();
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            param = settings.value(key).toFloat();
        }
        else if constexpr (std::is_same_v<T, std::pair<int, int>>)
        {
            auto list = settings.value(key).toList();
            Q_ASSERT(list.size() == 2);

            param = std::pair(list[0].toInt(), list[1].toInt());
        }
        else if constexpr (std::is_same_v<T, std::pair<float, float>>)
        {
            auto list = settings.value(key).toList();
            Q_ASSERT(list.size() == 2);

            param = std::pair(list[0].toFloat(), list[1].toFloat());
        }
        else if constexpr (std_array_traits<T>::value)
        {
            auto list = settings.value(key).toList();
            Q_ASSERT(list.size() == std_array_traits<T>::size);
            
            T newArray;
            if constexpr (std::is_same_v<T::value_type, int>)
                for (std::size_t i = 0; i < newArray.size(); i++) 
                    newArray[i] = list[i].toInt();
            else if constexpr (std::is_same_v<T::value_type, float>)
                for (std::size_t i = 0; i < newArray.size(); i++) 
                    newArray[i] = list[i].toFloat();

            param = newArray;
        }
        else if constexpr (std::is_same_v<T, ParameterData>)
        {
            auto list = settings.value(key).toList();
            Q_ASSERT(list.size() == 4);

            param = ParameterData{ {list[0].toFloat(), list[1].toFloat()}, list[2].toFloat(), list[3].toFloat() };
        }
        else
        {
            omnigen_assert("Variable type is not being handled!");
        }
    };

    settings.beginGroup("landform");
    auto landFormGroups = settings.childGroups();

    for (auto landForm : magic_enum::enum_values<ELandformVariations>())
    {
        auto name = QString::fromStdString(std::string(magic_enum::enum_name<ELandformVariations>(landForm)));
        Q_ASSERT(landFormGroups.contains(name, Qt::CaseSensitivity::CaseSensitive));

        settings.beginGroup(name);

        LandformParams landFormParams;

        SET_VALUE(landFormParams, minSquares);
        SET_VALUE(landFormParams, ridgeMargin);
        SET_VALUE(landFormParams, ridgeDensityPerSquare);
        SET_VALUE(landFormParams, ridgeAverageSize);
        SET_VALUE(landFormParams, mainRidgeSize);
        SET_VALUE(landFormParams, subRidgeSize);
        SET_VALUE(landFormParams, mainPeakCount);
        SET_VALUE(landFormParams, peakDistance);
        SET_VALUE(landFormParams, ridgeMaxTreeLevel);
        SET_VALUE(landFormParams, IsohypseDropAngle);
        SET_VALUE(landFormParams, IsohypseCurveRatio);
        SET_VALUE(landFormParams, ridgelineSlopeAngle);
        SET_VALUE(landFormParams, slopeAngleSameRidgesLevel0);
        SET_VALUE(landFormParams, slopeAngleSameRidges);
        SET_VALUE(landFormParams, slopeAngleDifferentRidges);
        SET_VALUE(landFormParams, slopeFactorRange);
        SET_VALUE(landFormParams, segmentLength);
        SET_VALUE(landFormParams, scaling);
        SET_VALUE(landFormParams, randomizedIncrement);

        PLandformTypes[landForm] = landFormParams;

        settings.endGroup();
    }

    settings.endGroup();

    settings.beginGroup("tableland");
    auto tablelandGroups = settings.childGroups();

    for (auto tablelandVariation : magic_enum::enum_values<ELandformVariations>())
    {
        auto name = QString::fromStdString(std::string(magic_enum::enum_name<ELandformVariations>(tablelandVariation)));

        if (!name.contains("Tablelands", Qt::CaseSensitivity::CaseSensitive))
            continue;

        Q_ASSERT(tablelandGroups.contains(name, Qt::CaseSensitivity::CaseSensitive));

        auto&& blah = settings.childKeys();
        auto&& tablelandVariations = settings.childGroups();

        for (auto&& variation : tablelandVariations)
        {
            settings.beginGroup(variation);
            std::unordered_map<ETableLand, TablelandParams> tablelandParamMap;

            for (auto tableland : magic_enum::enum_values<ETableLand>())
            {
                auto TablelandName = QString::fromStdString(std::string(magic_enum::enum_name<ETableLand>(tableland)));

                settings.beginGroup(TablelandName);

                TablelandParams tablelandParams;

                SET_VALUE(tablelandParams, flatRadius);
                SET_VALUE(tablelandParams, dropRatio);
                SET_VALUE(tablelandParams, ridgeDensityRatio);
                SET_VALUE(tablelandParams, desiredFormSize);
                SET_VALUE(tablelandParams, desiredPrecipiceSteps);
                SET_VALUE(tablelandParams, randomizationStart);

                tablelandParamMap[tableland] = tablelandParams;

                settings.endGroup();
            }

            settings.endGroup();
            PTablelandTypes[tablelandVariation] = tablelandParamMap;
        }
    }

    settings.endGroup();

    settings.beginGroup("ridge");

    {
        RidgeCharacter ridgeParams;

        settings.beginGroup("Size");
        auto ridgeSizeKeys = settings.childKeys();

        for (auto ridgeSize : magic_enum::enum_values<ERidgeSize>())
        {
            auto name = QString::fromStdString(std::string(magic_enum::enum_name<ERidgeSize>(ridgeSize)));
            Q_ASSERT(ridgeSizeKeys.contains(name, Qt::CaseSensitivity::CaseSensitive));

            setValue(ridgeParams.size[ridgeSize], name);
        }
        settings.endGroup();

        settings.beginGroup("ComplexityMain");
        auto ridgeMainComplexityKeys = settings.childKeys();

        for (auto ridgeComplexity : magic_enum::enum_values<ERidgeComplexity>())
        {
            auto name = QString::fromStdString(std::string(magic_enum::enum_name<ERidgeComplexity>(ridgeComplexity)));
            Q_ASSERT(ridgeMainComplexityKeys.contains(name, Qt::CaseSensitivity::CaseSensitive));

            setValue(ridgeParams.complexityMain[ridgeComplexity], name);
        }
        settings.endGroup();

        settings.beginGroup("ComplexitySub");
        auto ridgeSubComplexityKeys = settings.childKeys();

        for (auto ridgeComplexity : magic_enum::enum_values<ERidgeComplexity>())
        {
            auto name = QString::fromStdString(std::string(magic_enum::enum_name<ERidgeComplexity>(ridgeComplexity)));
            Q_ASSERT(ridgeSubComplexityKeys.contains(name, Qt::CaseSensitivity::CaseSensitive));

            setValue(ridgeParams.complexitySub[ridgeComplexity], name);
        }
        settings.endGroup();

        settings.beginGroup("Spread");
        auto ridgeSpreadKeys = settings.childKeys();

        for (auto ridgeSpread : magic_enum::enum_values<ERidgeSpread>())
        {
            auto name = QString::fromStdString(std::string(magic_enum::enum_name<ERidgeSpread>(ridgeSpread)));
            Q_ASSERT(ridgeSpreadKeys.contains(name, Qt::CaseSensitivity::CaseSensitive));

            setValue(ridgeParams.spread[ridgeSpread], name);
        }
        settings.endGroup();

        PRidgeCharacter = ridgeParams;
    }

    settings.endGroup();

    settings.beginGroup("rivertype");
    auto rivertypeGroups = settings.childGroups();

    for (auto rivertype : magic_enum::enum_values<Generation::ERiverType>())
    {
        if (rivertype == Generation::ERiverType::Last)
            continue;

        auto name = QString::fromStdString(std::string(magic_enum::enum_name<Generation::ERiverType>(rivertype)));
        Q_ASSERT(rivertypeGroups.contains(name, Qt::CaseSensitivity::CaseSensitive));

        settings.beginGroup(name);

        RiverParams riverParams;

        SET_VALUE(riverParams, slopeAngleRange);
        SET_VALUE(riverParams, wdRatio);
        SET_VALUE(riverParams, sinusoity);
        SET_VALUE(riverParams, entrenchment);

        Generation::ERiverTypeConstexpr::UseIn<EAC::SetRiverTraits>(rivertype, riverParams);

        settings.endGroup();
    }

    settings.endGroup();

    /////////////////////////////////////////////////////////////////
    settings.beginGroup("temperature");

    for (auto temperature : magic_enum::enum_values<ETemperature>())
    {
        auto name = toQString(temperature, false);
        setValue(PTemperature[temperature], name);
    }

    settings.endGroup();

    /////////////////////////////////////////////////////////////////
    settings.beginGroup("humidity");

    for (auto humidity : magic_enum::enum_values<EHumidity>())
    {
        auto name = toQString(humidity, false);
        setValue(PHumidity[humidity], name);
    }

    settings.endGroup();

#undef SET_VALUE
}

void Omnigen::updateAll(QWidget* w)
{
    for (auto&& c : w->children())
        if (auto* cw = dynamic_cast<QWidget*>(c))
            updateAll(cw);

    w->update();
}

QWidget* Omnigen::createTitleWidget(const QString& title) const
{
    auto* titleWidget = new QFrame;

    titleWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* label = new QLabel(title);

    QHBoxLayout* layout = new QHBoxLayout(titleWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(label);
    layout->addSpacerItem(new QSpacerItem(3840, 0, QSizePolicy::Maximum));

    return titleWidget;
}

QWidget* Omnigen::beginOmnigenSection() const
{
    auto* section = new QFrame();
    section->setLineWidth(1);
    //section->setLayout(new QVBoxLayout);
    //section->layout()->setContentsMargins(0, 0, 0, 0);
    //section->layout()->setSpacing(0);

    return section;
}

void Omnigen::finalizeOmnigenSection(const QString& name, EOmnigenSection section, SectionWidget* parentSection, DropArea area)
{
    auto result = SectionContent::newSectionContent(name, mainContainer, createTitleWidget(name), sectionWidgets[section]->parentWidget());

    if (area != InvalidDropArea)
    {
        mainContainer->addSectionContent(result, parentSection, area);
    }
    else
    {
        mainContainer->addSectionContent(result, parentSection, TopDropArea);
        mainContainer->hideSectionContent(result);
    }

    sections[section] = result;
}
