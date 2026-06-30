#include "stdafx.h"
#include "OmnigenViewportSection.h"
#include "Omnigen.h"
#include "Editor/Sections/CameraSystem/OmnigenCameraSection.h"

#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Common/Markers/SharedMeshMarker.h"
#include "Scene/Generation/Common/Markers/PointCloudMarker.h"
#include "Scene/Generation/Common/Markers/PolygonMarker.h"
#include "Scene/Generation/Stages/Ridges/RidgeMarker.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"
#include "Scene/Generation/Stages/Landmasses/ShorelineMarker.h"
#include "Scene/Generation/Stages/Landmasses/SeamassMarker.h"
#include "Scene/Generation/Stages/Landmasses/LandmassMarker.h"
#include "Scene/Generation/Stages/TerrainMods/River/RiverMarker.h"
#include "Scene/Generation/Stages/TerrainModel/DigitalElevationModel.h"
#include "Scene/Generation/Stages/Foliage/PlantDrawable.h"
#include "Scene/Generation/Stages/TerrainMods/Desert/SandSurfaceMarker.h"
#include "Scene/Generation/Stages/Lithomap/LithomapMarker.h"

QOmnigenViewportSection::QOmnigenViewportSection(QWidget* parent, EOmnigenSection inSection)
    : QWidget(nullptr)
    , section(inSection)
{
    auto* sectionLayout = new QVBoxLayout(parent);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(0);

    setLayout(new QVBoxLayout());
    layout()->setContentsMargins(0, 0, 0, 0);
    layout()->setSpacing(0);

    static_assert(int(EOmnigenSection::Viewport1) == 0);
    int idx = int(section);
    Q_ASSERT(std::clamp(idx, 0, 3) == idx);
    bool isMain = (idx == 0);

    auto* viewport = new QOmnigenViewport(nullptr, isMain, idx);
    auto* viewportHUD = createViewportMenuBar();

    viewport->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));

    layout()->addWidget(viewportHUD);
    layout()->addWidget(viewport);

    viewports[idx] = viewport;

    sectionLayout->addWidget(this);
}

void QOmnigenViewportSection::changeActiveViewport(QOmnigenViewport* viewport)
{
    getActiveViewport()->setIsActiveViewport(false);
    getActiveViewport()->setUpdatesEnabled(false);

    viewport->setIsActiveViewport(true);
    viewport->setUpdatesEnabled(true);

    activeIdx = viewport->getViewportIndex();

    Omnigen::get()->getCameraSection()->selectFocusedCamera(OmnigenCameraMgr::get()->getActiveCameraName(activeIdx));
    QOmnigenViewportSection::repaintAll(true);
}

void QOmnigenViewportSection::repaintAll(bool forceRefresh /*= false*/)
{
    if (forceRefresh)
    {
        auto&& activeCamsMap = OmnigenCameraMgr::get()->getAllActiveCameras();
        int activeVpIdx = getActiveViewport()->getViewportIndex();
        auto&& activeCamName = OmnigenCameraMgr::get()->getActiveCameraName(activeVpIdx);

        for (auto&& kv = activeCamsMap.keyValueBegin(); kv != activeCamsMap.keyValueEnd(); kv++)
        {
            // Since a single camera can be the active cam for multiple viewports, non-focused duplicates must be skipped
            if ((*kv).second == activeCamName && (*kv).first != activeVpIdx)
                continue;

            auto&& cam = OmnigenCameraMgr::get()->getCamera((*kv).second);
            auto&& vp = viewports.value((*kv).first);
            cam->setViewportSize({ float(vp->width()), float(vp->height()) });
        }
    }

    for (auto&& vp : viewports)
    {
        vp->singleDrawBegin();
        vp->update(0, 0, Omnigen::get()->width(), Omnigen::get()->height());
    }
}

template<typename... Ts>
void makeToggle(QOmnigenViewportSection* sectionWidget, const QString& label, QMenu* menu, bool init = true)
{
    int vIdx = int(sectionWidget->section);

    auto&& toggle = QOmnigenViewportSection::renderToggles<Ts...>[vIdx];
    toggle = new QCheckBox(label, sectionWidget);

    auto* action = new QWidgetAction(sectionWidget);
    action->setDefaultWidget(toggle);

    toggle->setChecked(init);
    forAll<Ts...>([=]<typename T>() { bShouldDraw<T>[int(sectionWidget->section)] = init; });

    QObject::connect(toggle, &QCheckBox::stateChanged, sectionWidget, [vIdx, toggle]()
        {
            forAll<Ts...>([=]<typename T>() { bShouldDraw<T>[vIdx] = toggle->isChecked(); });
        });

    menu->addAction(action);
};

void QOmnigenViewportSection::saveToggles(OmniBin<std::ios::out>& omniBin)
{
    auto saveToggle = [&]<typename T>() { omniBin << bShouldDraw<T>; };

    forAll<DShorelineMarker, DSeamassMarker, DLandmassMarker>(saveToggle);
    forAll<DRidgeMarker, DBatchingIsohypseMarker>(saveToggle);
    forAll<DDemMarker >(saveToggle);
    forAll<DLithomapMarker>(saveToggle);
    forAll<DClusterMeshMarker>(saveToggle);
    forAll<Generation::DPlant>(saveToggle);
    forAll<DLineMarker, DPointCloudMarker, DSharedMeshMarker<>>(saveToggle);
    forAll<DRiverMarker, DTrueRiverBoundMarker, DSandSurfaceMarker>(saveToggle);
}

void QOmnigenViewportSection::loadToggles(OmniBin<std::ios::in>& omniBin)
{
    auto loadToggle = [&]<typename T>() { omniBin >> bShouldDraw<T>; };
    auto loadSetToggle = [&]<typename... Ts>() 
    { 
        forAll<Ts...>(loadToggle);
        static_assert(sizeof...(Ts) > 0);

        using T = std::tuple_element_t<0, std::tuple<Ts...>>;
        auto&& bs = bShouldDraw<T>;
        auto&& toggles = renderToggles<Ts...>;
        for (int i = 0; i < 4; ++i)
            toggles[i]->setChecked(bs[i]);
    };

    loadSetToggle.operator()<DShorelineMarker, DLandmassMarker, DSeamassMarker>();
    loadSetToggle.operator()<DRidgeMarker, DBatchingIsohypseMarker>();
    loadSetToggle.operator()<DDemMarker>();
    loadSetToggle.operator()<DLithomapMarker>();
    loadSetToggle.operator()<DClusterMeshMarker>();
    loadSetToggle.operator()<Generation::DPlant>();
    loadSetToggle.operator()<DLineMarker, DPointCloudMarker, DSharedMeshMarker<>>();
    loadSetToggle.operator()<DRiverMarker, DTrueRiverBoundMarker, DSandSurfaceMarker>();
}

QWidget* QOmnigenViewportSection::createViewportMenuBar()
{
    auto* vMenuBar = new QMenuBar();

    // Temporary stylesheet since there will be no MenuBar under Viewport in final design
    vMenuBar->setStyleSheet("QMenuBar {background: #454545; padding: 0px;} QMenuBar::item{background-color:#454545;}");

    // Render menu
    auto* renderMenu = new QMenu("Render");

    makeToggle<DShorelineMarker, DLandmassMarker, DSeamassMarker>(this, "Landmasses", renderMenu);
    makeToggle<DRidgeMarker, DBatchingIsohypseMarker>(this, "Ridges and Isohypses", renderMenu);
    makeToggle<DDemMarker>(this, "DEM", renderMenu);
    makeToggle<DLithomapMarker>(this, "Lithomap", renderMenu);
    makeToggle<DClusterMeshMarker>(this, "Terrain Geometry", renderMenu);
    makeToggle<Generation::DPlant>(this, "Foliage", renderMenu);
    makeToggle<DLineMarker, DPointCloudMarker, DSharedMeshMarker<>>(this, "Debug", renderMenu);
    makeToggle<DRiverMarker, DTrueRiverBoundMarker, DSandSurfaceMarker>(this, "Mod helpers", renderMenu);

    vMenuBar->addMenu(renderMenu);

    return vMenuBar;
}
