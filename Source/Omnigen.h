#pragma once
#include <QtWidgets/QMainWindow>
#include <QSharedPointer>
#include <QDir>
#include "Constants.h"
#include "Editor/Framework/Style/FramelessWindow/FramelessWindow.h"
#include "Editor/Framework/AdvancedDockingSystem/ContainerWidget.h"
#include "Scene/Generation/OmnigenGenerationData.h"

class QVBoxLayout;
class QOmnigenViewport;
class QOmnigenPropertiesSection;
class QOmnigenOutlineSection;
class QOmnigenProfilerSection;
class QOmnigenHistoryStackSection;
class QOmnigenAssetMgrSection;
class QOmnigenLithologySection;
class QShortcut;
class OmnigenLogMessageItemModel;
class QOmnigenCameraSection;
class DDomainPaintingPreview;
class QOmniStageBar;
class DEditorGrid;
class DSkybox;
class DDomain;
class DManipulationGizmo;

namespace Generation
{
    class Data;
}

enum class EOmnigenAction
{
    New,
    Open,
    Save,
    SaveAs,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    ResetView,
    Preferences,
    ResetLayout,
    ToggleViewport1,
    ToggleViewport2,
    ToggleViewport3,
    ToggleViewport4,
    ToggleOutline,
    ToggleProperties,
    ToggleHistoryStack,
    ToggleLog,
    ToggleProfiler,
    ToggleCameraSystem,
    ToggleAssetMgr,
    EndToggles, // artificial, do not use
    About,
    DeveloperResume,

    Generate,
    Regenerate,
    ToggleStageBar,
};

enum class EOmnigenSection
{
    Viewport1,
    Viewport2,
    Viewport3,
    Viewport4,
    Outline,
    Properties,
    HistoryStack,
    Log,
    Profiler,
    CameraSystem,
    AssetMgr,
};

class Omnigen : public QFramelessWindow
{
    Q_OBJECT

    inline static Omnigen* sInstance = nullptr;
    static QString sConfigName;
    static QString sGenerationConfigName;
    static QString sDefaultConfigName;

public:
    static inline Omnigen* get()
    {
        if (!sInstance)
            sInstance = new Omnigen();

        return sInstance;
    }

    Omnigen(QWidget *parent = Q_NULLPTR);

    void initialize();

    QOmnigenPropertiesSection* getProperties();
    QOmnigenOutlineSection* getOutline();
    QOmnigenHistoryStackSection* getHistoryStack();
    QOmnigenProfilerSection* getProfiler();
    QOmnigenCameraSection* getCameraSection();
    QOmnigenAssetMgrSection* getAssetsSection();

    const std::map<ELOD, float>& getDistanceMap() const;
    const auto& getActions() const { return actions; };
    const QMap<int, QOmnigenViewport*>& getAllViewports() const;
    const auto& getStageBar() const { return stageBar; };

    std::vector<QSet<GPoint>> partitionSquares(QSet<GPoint> squares);

    bool isSectionVisible(EOmnigenSection s) const;
    void toggleSectionVisibility(EOmnigenSection s);
    bool isGenerating() const;
    QDir getProjectDir();
    void autoSave(EGenerationStage currentStage);

signals:
    void toggleShortcut(bool enable);
    void configLoaded();

public slots:
    void preClose();

    // File menu
    void action_new();
    void action_save(bool isSaveAs = false);
    void action_open();

    // Edit menu
    void action_undo();
    void action_redo();
    void action_cut();
    void action_copy();
    void action_paste();

    // View menu
    void action_resetViewport();
    void action_preferences();

    // Window menu
    void action_resetWindowLayout();

    // Help menu
    void action_about();
    void action_resume();

    // Toolbar
    EGenerationStage action_generate(EGenerationStage targetStage);
    void action_generateAll();
    void action_regenerateAll();

private:
    void action_save_impl(const std::string& filename);
    void saveConfig(const QString& filename = sConfigName);
    void loadConfig(const QString& filename = sConfigName);
    void loadGenerationParams(const QString& filename = sGenerationConfigName);

    void updateAll(QWidget* w);
    QWidget* createTitleWidget(const QString& title) const;
    QWidget* beginOmnigenSection() const;
    void finalizeOmnigenSection(const QString& name, EOmnigenSection section, SectionWidget* parentSection, DropArea area);
    QWidget* createMenuBar();
    QWidget* createMainToolbar();
    QWidget* createInitialContent();
    QWidget* createGenerationStageBar();
    void clearAll();
    void setupEvents();
    void updateToggleActionStates();
    void setupShortcuts();

    void showWelcomeScreen();

    void registerDrawable(size_t typeHash, QSharedPointer<Editable> e);
    void unregisterDrawable(QSharedPointer<Editable> e);

    // Main widget
    ContainerWidget* mainContainer = nullptr; // can't be shared_ptr

    // Framework classes not related to the real functionality.
    QMap<EOmnigenSection, QSharedPointer<SectionContent>> sections; 
    // Section widgets (real logic)
    QMap<EOmnigenSection, QWidget*> sectionWidgets;

    // Data
    QSharedPointer<DEditorGrid> grid;
    QSharedPointer<DSkybox> skybox;
    QSharedPointer<DManipulationGizmo> gizmo;
    QSharedPointer<OmnigenLogMessageItemModel> logMessages;
    QAction* lastViewportWindow = nullptr;

    // Actions
    QMap<EOmnigenAction, QAction*> actions;
    QOmniStageBar* stageBar = nullptr;

    // Config
    QString projectFileName;
    std::optional<EGenerationStage> autosaveEndStage;

    // Flags
    bool bIsGenerating = false;

    //Shortcuts
    QShortcut* undoShortcut;
    QShortcut* redoShortcut;
    QShortcut* generateAllShortcut;
    QShortcut* generateStepBackShortcut;
    QShortcut* generateStepForwardShortcut;
    QShortcut* regenerateCurrentStageShortcut;
    QShortcut* regenerateAllShortcut;

    friend struct QOmnigenPreferencesDialog;
    friend class Generation::Data;
};
