#include "stdafx.h"
#include "ShorelineMarker.h"
#include "ShorelineUtils.h"
#include "Utils/OmnigenDirectCompute.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "Utils/SquigCurve.h"
#include "Editor/Sections/Profiler/OmnigenProfiler.h"
#include "Editor/StageTools/Landmasses/LandmassSelection.h"
#include "Omnigen.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/Layout/StageGeneration_Layout.h"
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

#define DEBUG_BAYS_ALL 0
#define DEBUG_BAYS_GRAPH 0
#define DEBUG_BAYS_TOP 0
#define DEBUG_ISLANDS 0
#define DEBUG_ENDPOINTS 0
#define DEBUG_AREAS 0

#if DEBUG_ISLANDS
// mark a point
void markPoint(GVector2D point, QVector4D color = QVector4D(0.5, 0.25, 0.75, 1), int height = 20000)
{
    Generation::Data::get()->createMarker<DLineMarker>(std::vector{ QVector3D(point.x, 0, point.z) , QVector3D(point.x, height, point.z) }, color);
};

void markPointV(QVector3D point, QVector4D color = QVector4D(0.6, 0.4, 0.75, 1), int height = 20000)
{
    Generation::Data::get()->createMarker<DLineMarker>(std::vector{ point, point + QVector3D(0, height, 0) }, color);
};

void markPath(std::vector<QVector3D> path, QVector4D color, int height = 5000)
{
    for (auto&& point : path)
        markPointV(point, color, height);
};

void markEndpoints(std::vector<GPoint> endpoints, QVector4D color = QVector4D(0.5, 0.25, 0.75, 1), int height = 20000)
{
    for (auto&& point : endpoints)
        markPoint(GVector2D(point.x + 0.5f, point.z + 0.5f) * GRID_SEGMENT_WIDTH, color, height);
};

// mark a set of GPoints 
void markSet(const QSet<GPoint>& set, QVector4D color, int height = 5000)
{
    for (auto&& point : set)
        markPoint(GVector2D(point.x + 0.5f, point.z + 0.5f) * GRID_SEGMENT_WIDTH, color, height);
};
#endif

// Generalize usage of those functions
template <bool checkCorners = false, typename F>
void for_each_sq_neighbor(const QSet<GPoint>& set, const F& func)
{
    for (auto&& sq : set)
        for (int x = -1; x <= 1; x++)
            for (int z = -1; z <= 1; z++)
                if ((checkCorners && x != 0 && z != 0) || x != z)
                    func(sq, GPoint(sq.x + x, sq.z + z));
}

template <int layer = 1, typename F>
void for_each_sq_around(const QSet<GPoint>& set, const F& func)
{
    for (auto&& sq : set)
        for (int x = -layer; x <= layer; x++)
            for (int z = -layer; z <= layer; z++)
                func(GPoint(sq, sq.x + x, sq.z + z));
}

DShorelineMarker::DShorelineMarker(const std::vector<QVector3D>& inControlPoints, bool isCircular)
    : DLineMarker(inControlPoints, QVector4D(0.7, 0.7, 1, 1), isCircular, 110)
    , segmentWidth(distance(inControlPoints[0],inControlPoints[1]))
    , shorelineHeights(inControlPoints.size(), 1)
{
    makeName();
    selectionColor = color - QVector4D(0.33f, 0.33f, 0.33f, 0.0f);

    for (auto it = std::next(inControlPoints.begin()); it != std::prev(inControlPoints.end()); it++)
    {
        auto gp = ((GVector2D)*it).toGPoint();
        Q_ASSERT(Generation::Data::get()->getDomainAtSquare(gp, EDomainType::Terrain));
        
        squares += gp;
    }
}

void DShorelineMarker::draw()
{
    setHovered(Design::ShorelineSelection::isShorelineHovered(sharedFromThis()));

    DLineMarker::draw();
}

void DShorelineMarker::showAs3D()
{
    auto cPts = getControlPoints();
    Q_ASSERT(cPts.size() == shorelineHeights.size());
    tbb::parallel_for(0, int(cPts.size()), [&](int i)
        {
            cPts[i].setY(shorelineHeights[i]);
        });

    movePoints(cPts);
}

void DShorelineMarker::showAs2D()
{
    auto cPts = getControlPoints();
    tbb::parallel_for(0, int(cPts.size()), [&](int i)
        {
            cPts[i].setY(110.0f);
        });

    movePoints(cPts);
}

void DShorelineMarker::makeName()
{
    static int nameCounter = 0;
    name = "Shoreline " + QString::number(++nameCounter);
}

void DShorelineMarker::setName(const QString& newName)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    name = newName;
    emit Editable::modified(sharedFromThis());
}

void DShorelineMarker::setSegmentWidth(float newSegmentWidth)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    segmentWidth = newSegmentWidth;
    emit Editable::modified(sharedFromThis());
}

void DShorelineMarker::setLandmass(const QSharedPointer<DLandmassMarker>& newLandmass)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    landmass = newLandmass;
    landmassGuid = newLandmass ? newLandmass->getGuid() : -1;
    emit Editable::modified(sharedFromThis());
}

void DShorelineMarker::setBays(const QSharedPointer<BayNode>& newBays)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    baysRoot = newBays;
    emit Editable::modified(sharedFromThis());
}

void DShorelineMarker::setPeninsulas(const QSharedPointer<BayNode>& newPeninsulas)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    peninsulasRoot = newPeninsulas;
    emit Editable::modified(sharedFromThis());
}

void DShorelineMarker::setSquares(const QSet<GPoint>& newSquares)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    squares = newSquares;
    emit Editable::modified(sharedFromThis());
}

void DShorelineMarker::setPoints(const std::vector<QVector3D>& newVerts, bool isLoop /*= false*/)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    DLineMarker::setPoints(newVerts, isLoop);
    shorelineHeights.clear();
    shorelineHeights = std::vector<float>(newVerts.size(), 1);

    squares.clear();
    for (auto it = std::next(newVerts.begin()); it != std::prev(newVerts.end()); it++)
        squares += ((GVector2D)*it).toGPoint();
    emit Editable::modified(sharedFromThis());
}

void DShorelineMarker::setHeights(std::vector<float> newHeights)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    auto& vertices = getActiveGeometry()->vertices;

    Q_ASSERT(newHeights.size() == vertices.size());

    shorelineHeights = newHeights;
    for (int i = 0; i < newHeights.size(); i++)
        vertices[i].setY(newHeights[i]);

    updateVbo(activeLOD);
    cacheBoundingBox();
    computeBoxVerts();
    emit Editable::modified(sharedFromThis());
}


std::vector<QSharedPointer<BayNode>> DShorelineMarker::getPeninsulaPath(int pointIdx) const
{
    std::vector<QSharedPointer<BayNode>> result;
    std::vector<QSharedPointer<BayNode>> currentNodes = peninsulasRoot->getChildren();

    while (true)
    {
        bool found = false;

        for (auto&& pen : currentNodes)
        {
            if (pen->contains(pointIdx))
            {
                result << pen;
                currentNodes = pen->getChildren();
                found = true;
                break;
            }
        }

        if (!found)
            break;
    }

    return result;
}

std::vector<std::vector<QSharedPointer<DShorelineMarker>>> DShorelineMarker::generateBasicShorelines(const QSet<GPoint>& landSquares, const QSet<GPoint>& seasideSquares)
{
    auto landPolygons = PolygonUtils::calculatePolygonsFromGridSquares(landSquares);
    auto seasidePolygons = PolygonUtils::calculatePolygonsFromGridSquares(seasideSquares);

    for (auto&& landPolygon : landPolygons)
    {
        auto inflatedLand = PolygonUtils::inflatePolygon(landPolygon, 1.0f).front();
        auto shorelinePolygons = PolygonUtils::intersectPolygons(inflatedLand, seasidePolygons);
        shorelinePolygons << landPolygon;
        landPolygon = PolygonUtils::mergePolygons(shorelinePolygons).front();
    }
    landPolygons = PolygonUtils::mergePolygons(landPolygons);

    std::vector<std::vector<QSharedPointer<DShorelineMarker>>> shorelinesPerLand;

    for (auto&& landPolygon : landPolygons)
    {
        auto&& [newShorelines, isCoast] = ShorelineUtils::findShorelinesAlongLandmass(landPolygon, seasidePolygons);
        ShorelineUtils::reduceDistanceBetweenPoints(&newShorelines, 250.0f, isCoast);

        std::vector<QSharedPointer<DShorelineMarker>> shorelineMarkers;
        for (auto&& shoreline : newShorelines)
            shorelineMarkers << spawn<DShorelineMarker>(shoreline, !isCoast);

        shorelinesPerLand << shorelineMarkers;
    }

    return shorelinesPerLand;
}

std::vector<std::vector<QSharedPointer<DShorelineMarker>>> DShorelineMarker::generateInitBasicShorelines(const std::optional<std::vector<QSharedPointer<DLandmassMarker>>>& existingLandmasses /*= std::nullopt*/)
{
    QSet<GPoint> terrainSquares = Generation::Data::get()->getAllSquares<EDomainType::Terrain>();
    QSet<GPoint> seaSquares = Generation::Data::get()->getAllSquares<EDomainType::Water>();

    QSet<GPoint> assignedLandSquares;
    auto&& landmasses = existingLandmasses ? *existingLandmasses : Generation::Data::get()->getMarkers<DLandmassMarker>();
    for (auto&& landmass : landmasses)
        assignedLandSquares += landmass->getSquares();

    QSet<GPoint> landSquares = (terrainSquares - seaSquares) - assignedLandSquares;
    QSet<GPoint> seasideSquares = container_and(terrainSquares, seaSquares);

    return generateBasicShorelines(landSquares, seasideSquares);
}

void DShorelineMarker::generateInit()
{
    auto&& existingLandmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();
    std::vector<QSharedPointer<DLandmassMarker>> landmasses;

    auto&& shorelinesPerLand = generateInitBasicShorelines(existingLandmasses);
    for (auto&& shorelines : shorelinesPerLand)
    {
        auto&& landmass = spawn<DLandmassMarker>(shorelines, std::vector<QSharedPointer<DShorelineMarker>>{});
        for (auto&& shorelineMarker : shorelines)
            shorelineMarker->setLandmass(landmass);

        landmasses << landmass;
    }

    landmasses << existingLandmasses;
    Generation::Data::get()->clearExactMarkers<DSeamassMarker>();
    DSeamassMarker::generateSeamassMarkers(landmasses);
}

bool DShorelineMarker::generateAll()
{
    OmniProfile("Shorelines");

    Generation::gDirectCompute.setShader(L"Resources/Shaders/Compute/BaysRecognition.hlsl");

    auto&& existingLandmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();
    for (auto&& landmass : existingLandmasses)
        if (!landmass->isLocked())
        {
            Generation::Data::get()->clearSingleExactMarker<DLandmassMarker>(landmass->getGuid());
            landmass->forEachShoreline([](auto& s, bool isInner) 
                { Generation::Data::get()->clearSingleExactMarker<DShorelineMarker>(s->getGuid()); });
        }
    existingLandmasses = Generation::Data::get()->getMarkers<DLandmassMarker>();

    std::vector<Landmass> allLandmassAreas;

    auto lands = Generation::Utils::findLandAreas();

    allLandmassAreas = createOriginLandmasses(lands, existingLandmasses);

    QSet<GPoint> landmassLand;
    for (auto&& landmass : allLandmassAreas)
        landmassLand += landmass.insideArea;
    for (auto&& landmass : existingLandmasses)
    {
        landmassLand += landmass->getSquares();
        landmass->forEachShoreline([&](auto& s, bool isInner) { landmassLand += s->getSquares(); });
    }

    auto seasides = Generation::Utils::findSeasideAreas(landmassLand);
    for (auto&& seaside : seasides)
    {
        // ignore inner seas seasides
        if (std::any_of(allLandmassAreas.begin(), allLandmassAreas.end(), [&](auto& lm) { return lm.innerSeaShorelineWithinSeaside(seaside); }))
            continue;

        const GPoint p = *seaside.begin();
        auto domainData = Generation::Data::get()->getDomainAtSquare(p, EDomainType::Water)->getData<EDomainType::Water>();
        IslandParameters params;

        params.coverage = PIslandsRatioCoverage[domainData->landCoverage];
        params.smallIslandsQuantity = PIslandsWeightQuanity[domainData->amountOfSmallIslands];
        params.mediumIslandsQuantity = PIslandsWeightQuanity[domainData->amountOfMediumIslands];
        params.largeIslandsQuantity = PIslandsWeightQuanity[domainData->amountOfLargeIslands];
        params.shorelineComplexity = PShorelineComplexity[domainData->shorelineComplexity];

        generateRandomLandmassAreas(&allLandmassAreas, seaside, params, SquareType::Terrain, false);
    }

    tbb::parallel_for(0, int(allLandmassAreas.size()), [&](int landmass_idx)
        {
            auto&& landmass = allLandmassAreas[landmass_idx];

            IslandParameters params;
            params.shorelineComplexity = PShorelineComplexity[EShorelineComplexity::Medium];

            generateShorelineMarkers(&landmass, params, SquareType::Terrain);
        });

    tbb::parallel_for(0, int(allLandmassAreas.size()), [&](int landmass_idx)
        {
            auto&& landmass = allLandmassAreas[landmass_idx];

            QSet<GPoint> landOnSeaside;
            for (auto&& p : landmass.insideArea)
                if (getSquareType(p) == SquareType::Seaside)
                    landOnSeaside << p;

            if (landOnSeaside.empty())
                return;

            auto domainData = Generation::Data::get()->getDomainAtSquare(*landOnSeaside.begin(), EDomainType::Water)->getData<EDomainType::Water>();
            IslandParameters params;

            params.coverage = 1.0f - PIslandsRatioCoverage[domainData->landCoverage];
            params.smallIslandsQuantity = 1.0f;
            params.mediumIslandsQuantity = 1.0f;
            params.largeIslandsQuantity = 1.0f;
            params.shorelineComplexity = PShorelineComplexity[domainData->shorelineComplexity];

            auto&& seamassesInsideLandmass = findSeamassesInLandmass(landmass);
            generateRandomLandmassAreas(&seamassesInsideLandmass, landOnSeaside, params, SquareType::Water, true);

            for (auto&& seamass : seamassesInsideLandmass)
            {
                generateShorelineMarkers(&seamass, params, SquareType::Water);

                landmass.insideArea -= seamass.insideArea;
                for (auto&& shoreline : seamass.shorelines)
                {
                    landmass.insideArea -= shoreline.area;

                    if (seamass.isCoast)
                        landmass.shorelines << shoreline;
                    else
                        landmass.innerSeaShorelines << shoreline;
                }
            }
        });

    auto&& landmassMarkers = generateLandmassesMarkers(allLandmassAreas);
    landmassMarkers << existingLandmasses;

    Generation::Data::get()->clearExactMarkers<DSeamassMarker>();
    DSeamassMarker::generateSeamassMarkers(landmassMarkers);

    return true;
}

bool DShorelineMarker::detectBays()
{
    auto&& controlPoints = getControlPoints();

    baysRoot = QSharedPointer<BayNode>::create();
    baysRoot->range = { 0, int(controlPoints.size()) - 1 };

    peninsulasRoot = QSharedPointer<BayNode>::create();
    peninsulasRoot->range = { 0, int(controlPoints.size()) - 1 };

    std::vector<GPUPoint> gpuPoints(controlPoints.size());
    for (int i = 0; i < controlPoints.size(); ++i)
    {
        gpuPoints[i].x = controlPoints[i].x();
        gpuPoints[i].z = controlPoints[i].z();
    }

    Generation::gDirectCompute.setReadWriteBuffers({ GPUBufferData{gpuPoints.data(), quint64(gpuPoints.size()), sizeof(GPUPoint)} });
    Generation::gDirectCompute.run(gpuPoints.size(), 1, 1);

#if DEBUG_BAYS_ALL
    for (int i = 1; i < int(gpuPoints.size()) - 1; ++i)
    {
        static QVector4D color = {1,1,1,1};
        if (int p = gpuPoints[i].forwardPair; p != -1)
        {
            color = (gpuPoints[i].forwardType == 1) ? QVector4D{ 1, 0.5, 0, 1 } : QVector4D{ 0, 0.5, 1, 1 };
            QVector3D midpoint = (controlPoints[p] + controlPoints[i]) * 0.5f + QVector3D(0, distance(controlPoints[p],controlPoints[i]) * 0.5f, 0);
            Generation::Data::get()->createMarker<DLineMarker>(std::vector{ controlPoints[p], midpoint, controlPoints[i]}, color);
            Generation::Data::get()->createMarker<DLineMarker>(std::vector{ controlPoints[p] + QVector3D(0,20,0), controlPoints[i] + QVector3D(0,20,0) }, color);
        }
        if (int p = gpuPoints[i].backwardPair; p != -1)
        {
            color = (gpuPoints[i].backwardType == -1) ? QVector4D{ 1, 0.5, 0, 1 } : QVector4D{ 0, 0.5, 1, 1 };
            QVector3D midpoint = (controlPoints[p] + controlPoints[i]) * 0.5f + QVector3D(0, distance(controlPoints[p],controlPoints[i]) * 0.5f, 0);
            Generation::Data::get()->createMarker<DLineMarker>(std::vector{ controlPoints[p], midpoint, controlPoints[i] }, color);
            Generation::Data::get()->createMarker<DLineMarker>(std::vector{ controlPoints[p] + QVector3D(0,20,0), controlPoints[i] + QVector3D(0,20,0) }, color);
        }
    }
#endif

    for (int i = 0; i < gpuPoints.size(); ++i)
    {
        if (int p = gpuPoints[i].forwardPair; p != -1)
        {
            if (gpuPoints[i].forwardType == 1)
                addNewPeninsula(i, p);
            else
                addNewBay(i, p);
        }
        if (int p = gpuPoints[i].backwardPair; p != -1)
        {
            if (gpuPoints[i].backwardType == -1)
                addNewPeninsula(i, p);
            else
                addNewBay(i, p);
        }
    }

#if DEBUG_BAYS_TOP
    for (auto&& node : baysRoot->getChildren())
        Generation::Data::get()->createMarker<DLineMarker>(std::vector{ controlPoints[node->range.first] + QVector3D(0,20,0), controlPoints[node->range.second] + QVector3D(0,20,0) }, QVector4D{ 0, 0.5, 1, 1 });

    for (auto&& node : peninsulasRoot->getChildren())
        Generation::Data::get()->createMarker<DLineMarker>(std::vector{ controlPoints[node->range.first] + QVector3D(0,20,0), controlPoints[node->range.second] + QVector3D(0,20,0) }, QVector4D{ 1, 0.5, 0, 1 });
#endif

    return true;
}

void DShorelineMarker::addNewBay(int a, int b)
{
    addNewBayNode(a, b, baysRoot);
}

void DShorelineMarker::addNewPeninsula(int a, int b)
{
    addNewBayNode(a, b, peninsulasRoot);
}

void DShorelineMarker::addNewBayNode(int a, int b, QSharedPointer<BayNode> targetRoot)
{
    auto newNode = QSharedPointer<BayNode>::create();
    newNode->range = { std::min(a, b), std::max(a, b) };

    targetRoot->insert(newNode);

#if DEBUG_BAYS_GRAPH
    auto getNodeMidpoint = [&](const BayNode& node)
    {
        auto&& shorePts = getControlPoints();
        auto diff = shorePts[node.range.second] - shorePts[node.range.first];
        return shorePts[node.range.first] + (0.5 * diff);
    };

    if (newNode->getParent() != targetRoot)
    {
        auto root = newNode->getParent().lock();
        while (root->getParent())
            root = root->getParent();

        bool isPen = (getPeninsulas() == root);
        Generation::Data::get()->createMarker<DLineMarker>(std::vector{ getNodeMidpoint(*newNode), getNodeMidpoint(*newNode->getParent().lock()) }, QVector4D(isPen ? 1 : 0, 0, isPen ? 0 : 1, 1));
    }
#endif
}

QSharedPointer<DLandmassMarker> DShorelineMarker::generateLandmassAtSquare(const QSet<GPoint>& avaliableSeaside, const QSet<GPoint>& illegalInsideSquares, const GPoint& startSquare, EShorelineComplexity comlexity, int size)
{
    Landmass landmass{ .insideArea{startSquare}, .isOrigin = false, .isCoast = false };
    QSet<GPoint> takenSeaside{startSquare};

    QQueue<GPoint> queue;
    queue.enqueue(startSquare);

    auto&& addToQueue = [&](const GPoint& p, int x, int z)
    {
        const GPoint square = GPoint(p.x + x, p.z + z);

        if (avaliableSeaside.contains(square) && takenSeaside.size() <= size && !takenSeaside.contains(square) && !illegalInsideSquares.contains(square))
        {
            landmass.insideArea += square;
            takenSeaside += square;
            queue.enqueue(square);
        }
    };

    while (!queue.isEmpty())
    {
        const GPoint square = queue.dequeue();

        addToQueue(square, 1, 0);
        addToQueue(square, -1, 0);
        addToQueue(square, 0, 1);
        addToQueue(square, 0, -1);

        if (!landmass.isCoast)
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    landmass.isCoast = landmass.isCoast || getSquareType(square, x, z) == SquareType::Other;
    }

    assignMustHaveShorelineAreas(&landmass, avaliableSeaside, {});
    generateShorelineMarkers(&landmass, { .shorelineComplexity = PShorelineComplexity[comlexity] }, SquareType::Terrain);

    return generateLandmassMarker(landmass);
}

std::vector<QVector3D> DShorelineMarker::generateshorelinePath(const QSet<GPoint>& land, const QSet<GPoint>& seaside, const IslandParameters& params, bool isCoast)
{
    if (isCoast)
        return createCoastShorelinePath(land, seaside, params, SquareType::Terrain);
    else
        return createIslandShorelinePath(land, seaside, params, SquareType::Terrain);
}

std::optional<std::pair<GVector2D, GVector2D>> DShorelineMarker::findCoastEndpoints(const QSet<GPoint>& seaside, const QSet<GPoint>& island)
{
	QSet<GVector2D> pts;

	// Endpoints are located on the corners shared by 3 types of squares:
	// a) seaside square
	// b) non-seaside terrain square
	// c) empty square

	auto&& domainSquares = Generation::Data::get()->getDomainSquares();

	auto checkCorner = [&pts, &domainSquares, &seaside, &island](const GPoint& sq, int xadj, int zadj)
	{
		bool hasPureTerrain = false;
		bool hasEmpty = false;
		bool xok = false;
		bool zok = false;

		// Consider 4 squares
		// GPoint sq; (this)
		GPoint x0 = { sq.x + xadj, sq.z };
		GPoint z0 = { sq.x, sq.z + zadj };
		GPoint xz = { sq.x + xadj, sq.z + zadj };

		// x0
		if (x0.x >= 0 && x0.x < GRID_SEGMENT_COUNT)
		{
			xok = true;
			if (!seaside.contains(x0))
			{
				if (!domainSquares[x0.x][x0.z])
					hasEmpty = true;

                if (island.contains(x0))
					hasPureTerrain = true;
			}
		}
		else
		{
			hasEmpty = true;
		}

		// z0
		if (z0.z >= 0 && z0.z < GRID_SEGMENT_COUNT)
		{
			zok = true;
			if (!seaside.contains(z0))
			{
				if (!domainSquares[z0.x][z0.z])
					hasEmpty = true;

                if (island.contains(z0))
					hasPureTerrain = true;
			}
		}
		else
		{
			hasEmpty = true;
		}

		// xz
		if (xok && zok && !seaside.contains(xz))
		{
			if (!domainSquares[xz.x][xz.z])
				hasEmpty = true;
            
            if (island.contains(xz))
				hasPureTerrain = true;
		}

		if (hasPureTerrain && hasEmpty)
		{
			// Target vertex location
			if (xadj < 0)
				xadj = 0;

			if (zadj < 0)
				zadj = 0;

			const auto p = GVector2D((sq.x + xadj) * GRID_SEGMENT_WIDTH, (sq.z + zadj) * GRID_SEGMENT_WIDTH);
			if (pts.contains(p))
				return false;

			pts << p;
			return true;
		}

		return false;
	};

    if (auto&& onlyNeighbourSeaside = ShorelineUtils::findOnlyNeighboringSquare(seaside, island); onlyNeighbourSeaside)
    {
        auto&& sq = *onlyNeighbourSeaside;
        
        checkCorner(sq, -1, -1);
        checkCorner(sq, 1, -1);
        checkCorner(sq, -1, 1);
        checkCorner(sq, 1, 1);
    }
    else
	    for (auto&& sq : seaside)
	    {
		    if (checkCorner(sq, -1, -1)) continue;
		    if (checkCorner(sq, 1, -1)) continue;
		    if (checkCorner(sq, -1, 1)) continue;
		    if (checkCorner(sq, 1, 1)) continue;
	    }

    return pts.size() > 1 ? std::pair(*pts.cbegin(), *pts.crbegin()) : std::optional<std::pair<GVector2D, GVector2D>>{};
}

DShorelineMarker::SquareType DShorelineMarker::getSquareType(const GPoint& p, int x, int z)
{
    const bool water = Generation::Data::get()->getDomainAtSquare({ p.x + x, p.z + z }, EDomainType::Water);
	const bool terrain = Generation::Data::get()->getDomainAtSquare({ p.x + x, p.z + z }, EDomainType::Terrain);

	if (water && !terrain)
		return SquareType::Water;
	else if (!water && terrain)
		return SquareType::Terrain;
	else if (water && terrain)
		return SquareType::Seaside;
	else
		return SquareType::Other;
}

std::vector<GVector2D> DShorelineMarker::findMidpointsPerLandmass(const std::vector<Landmass>& landmasses)
{
    static auto&& generate2DVector = [](GPoint coords)
    {
        return GVector2D(coords.x + 0.5f, coords.z + 0.5f) * GRID_SEGMENT_WIDTH;
    };

    std::vector<GVector2D> middles;

    for (auto&& landmass : landmasses)
        if (!landmass.insideArea.isEmpty())
            middles.push_back(generate2DVector(ShorelineUtils::findMidpoint(landmass.insideArea)));
        else
            middles.push_back({});

    return middles;
}

std::optional<int> DShorelineMarker::findLandmassSurroundingSet(const QSet<GPoint>& set, const QHash<GPoint, std::optional<int>>& landmassIndexAffiliation)
{
    QSet<int> surroundingLandmasses;

    auto checkPoint = [&](const GPoint& p)
    {
        if (auto islandIndex = landmassIndexAffiliation[p]; islandIndex)
            surroundingLandmasses += *islandIndex;

        return surroundingLandmasses.size() > 1 || getSquareType(p, 0, 0) == SquareType::Other;
    };

    for (auto&& p : set)
        if (checkPoint(GPoint(p.x + 2, p.z)) ||
            checkPoint(GPoint(p.x - 2, p.z)) ||
            checkPoint(GPoint(p.x, p.z + 2)) ||
            checkPoint(GPoint(p.x, p.z - 2)))
            return {};

    return !surroundingLandmasses.empty() ? *surroundingLandmasses.begin() : std::optional<int>{};
};

bool DShorelineMarker::checkAccess(const SquareType& excludedType, const GPoint& p, int x, int z)
{
    SquareType pType = getSquareType(p, x, z);
    return pType != excludedType && pType != SquareType::Seaside;
}

bool DShorelineMarker::checkOutsideAccess(const SquareType setType, const QSet<GPoint>& set)
{
    bool outsideAccess = false;

    QQueue<GPoint> queue;
    QSet<GPoint> visited;

    auto&& addToQueue = [&](const GPoint& p, int x, int z)
    {
        const GPoint point = GPoint(p.x + x, p.z + z);
        if (getSquareType(p, x, z) == setType && !visited.contains(point))
        {
            queue.enqueue(point);
            visited.insert(point);

            outsideAccess = outsideAccess
                || checkAccess(setType, point, 1, 0)
                || checkAccess(setType, point, -1, 0)
                || checkAccess(setType, point, 0, 1)
                || checkAccess(setType, point, 0, -1);
        }
    };

    for (auto&& point : set)
        addToQueue(point, 0, 0);

    while (!queue.isEmpty() && !outsideAccess)
    {
        const GPoint point = queue.dequeue();
        for (int x = -1; x <= 1; x++)
            for (int z = -1; z <= 1; z++)
                if (!(x == 0 && z == 0))addToQueue(point, x, z);
    }

    return outsideAccess;
};

std::tuple<QSet<GPoint>, QSet<GPoint>> DShorelineMarker::findOutsideTypePoints(const QSet<GPoint>& seasideToSplit, SquareType insideType)
{
    QSet<GPoint> outsideTypePoints;
    QSet<GPoint> outsideTypePointsBoundingPoints;

    QSet<GPoint> waterSet;
    QSet<GPoint> terrainSet;

    auto&& addToBound = [&](const GPoint& p, int x, int z)
    {
        GPoint point(p.x + x, p.z + z);
        auto pointType = getSquareType(p, x, z);

        if (pointType == SquareType::Water)
            waterSet.insert(point);
        else if (pointType == SquareType::Terrain)
            terrainSet.insert(point);
    };

    for (auto&& sq : seasideToSplit)
    {
        addToBound(sq, 1, 0);
        addToBound(sq, -1, 0);
        addToBound(sq, 0, 1);
        addToBound(sq, 0, -1);
    }

    auto&& findBoundingPoints = [&](const QSet<GPoint>& set, QSet<GPoint>* boundingSet)
    {
        for (auto&& p : set)
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    if (!(x == 0 && z == 0))
                    {
                        const GPoint point = GPoint(p.x + x, p.z + z);
                        if (seasideToSplit.contains(point))
                            boundingSet->insert(point);
                    }
    };

    if (insideType == SquareType::Terrain)
        outsideTypePoints = waterSet;
    else if (insideType == SquareType::Water)
        outsideTypePoints = terrainSet;

    findBoundingPoints(outsideTypePoints, &outsideTypePointsBoundingPoints);

    return std::tuple<QSet<GPoint>, QSet<GPoint>>(outsideTypePoints, outsideTypePointsBoundingPoints);
}

std::vector<Landmass> DShorelineMarker::createOriginLandmasses(const std::vector<QSet<GPoint>>& lands, const std::vector<QSharedPointer<DLandmassMarker>>& existingLandmasses)
{
    std::vector<Landmass> landmasses;

    for (auto&& land : lands)
    {
        if (std::any_of(existingLandmasses.begin(), existingLandmasses.end(), [&](auto& lm) { return lm->getSquares().contains(*land.begin()); }))
            continue;

        Landmass landmass{ .insideArea = land, .isOrigin = true, .isCoast = false };

        QSet<GPoint> shorelineAreas;
        for_each_sq_neighbor<true>(landmass.insideArea, [&](auto&& sq, auto&& n_sq) {
            if (getSquareType(n_sq) == SquareType::Seaside)
                shorelineAreas += n_sq;
            if (!landmass.isCoast && getSquareType(n_sq) == SquareType::Other)
                landmass.isCoast = true;
            });

        for (auto&& shorelineArea : ShorelineUtils::splitSeparateSet(shorelineAreas))
            landmass.shorelines.push_back(Shoreline{ .area = shorelineArea, .path = {} });

        landmasses << landmass;
    }

    return landmasses;
}

std::vector<Landmass> DShorelineMarker::findSeamassesInLandmass(const Landmass& landmass)
{
    std::vector<Landmass> seamasses;

    QSet<GPoint> seamassAreas;

    QQueue<GPoint> queue;
    for(auto && sq : landmass.insideArea)
        queue.enqueue(sq);

    auto&& addToQueue = [&](const GPoint& p, int x, int z)
    {
        const SquareType pointType = getSquareType(p, x, z);
        const GPoint point = GPoint(p.x + x, p.z + z);

        if (pointType == SquareType::Water)
        {
            seamassAreas += point;
            queue.enqueue(point);
        }
    };

    while (!queue.isEmpty())
    {
        const GPoint point = queue.dequeue();

        addToQueue(point, 1, 0);
        addToQueue(point, -1, 0);
        addToQueue(point, 0, 1);
        addToQueue(point, 0, -1);
    }


    for (auto&& seamassArea : ShorelineUtils::splitSeparateSet(seamassAreas))
    {
        Landmass seamass{ .insideArea = seamassArea, .isOrigin = true, .isCoast = false };

        QSet<GPoint> shorelineAreas;
        for_each_sq_neighbor<true>(seamass.insideArea, [&](auto&& sq, auto&& n_sq) {
            if (getSquareType(n_sq) == SquareType::Seaside)
                shorelineAreas += n_sq;
            if (!seamass.isCoast && getSquareType(n_sq) == SquareType::Other)
                seamass.isCoast = true;
            });

        for (auto&& shorelineArea : ShorelineUtils::splitSeparateSet(shorelineAreas))
            seamass.shorelines.push_back(Shoreline{ .area = shorelineArea, .path = {} });

        seamasses << seamass;
    }

    return seamasses;
}

void DShorelineMarker::populateSeasideWithVirtualLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const IslandParameters& islandParams)
{
    if (((*avaliableSeaside) - illegalPoints).isEmpty())
        return;

    (*avaliableSeaside) -= illegalPoints;

    QSet<GPoint> seasideNotCovered = *avaliableSeaside;

    auto removeCoveredSeasides = [&seasideNotCovered](const Landmass& landmass, int range)
    {
        for (auto&& sq : landmass.insideArea)
            for (int x = -range; x <= range; x++)
                for (int z = -range; z <= range; z++)
                    seasideNotCovered -= GPoint(sq.x + x, sq.z + z);
    };

    for (auto&& landmass : *landmasses)
        removeCoveredSeasides(landmass, 3);

    while (true)
    {
        if (seasideNotCovered.empty())
            break;

        int newIslandTargetSize = std::uniform_int_distribution<int>(1, 2)(Generation::gRandomEngine);
        int randomSeed = std::uniform_int_distribution<int>(0, int(seasideNotCovered.size()) - 1)(Generation::gRandomEngine);
        GPoint randomPoint = *(seasideNotCovered.begin() + randomSeed);

        generateVirtualLandmassFromPoint(landmasses, avaliableSeaside, landmassIndexAffiliation, randomPoint, newIslandTargetSize);

        removeCoveredSeasides(landmasses->back(), 3);
    }

    (*avaliableSeaside) += illegalPoints;
}

void DShorelineMarker::generateVirtualLandmassFromPoint(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const GPoint& startPoint, int targetIslandSize)
{
    auto checkNeighborAffiliation = [&](const GPoint& p)
    {
        for (int x = -3; x <= 3; x++)
            for (int z = -3; z <= 3; z++)
                if (auto islandIndex = (*landmassIndexAffiliation)[GPoint(p.x + x, p.z + z)]; islandIndex)
                    if (islandIndex != (int)landmasses->size())
                        return true;

        return false;
    };

    if (checkNeighborAffiliation(startPoint))
        return;

    Landmass landmass{ .insideArea{startPoint}, .isOrigin = false, .isCoast = false, .index = (int)landmasses->size() };
    (*landmassIndexAffiliation)[startPoint] = landmass.index;
    (*avaliableSeaside).remove(startPoint);
    targetIslandSize--;

    QQueue<GPoint> queue;
    queue.enqueue(startPoint);

    auto&& addToQueue = [&](const GPoint& p, int x, int z)
    {
        const SquareType pointType = getSquareType(p, x, z);
        const GPoint point = GPoint(p.x + x, p.z + z);

        if (targetIslandSize > 0 && pointType == SquareType::Seaside && (*avaliableSeaside).contains(point) && !checkNeighborAffiliation(point))
        {
            landmass.insideArea += point;
            (*landmassIndexAffiliation)[point] = landmass.index;
            (*avaliableSeaside).remove(point);
            targetIslandSize--;

            queue.enqueue(point);
        }
    };

    while (!queue.isEmpty())
    {
        const GPoint point = queue.dequeue();

        addToQueue(point, 1, 0);
        addToQueue(point, -1, 0);
        addToQueue(point, 0, 1);
        addToQueue(point, 0, -1);

        if (!landmass.isCoast)
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    landmass.isCoast = landmass.isCoast || getSquareType(point, x, z) == SquareType::Other;
    }

    landmasses->push_back(landmass);
}

std::optional<QSet<GPoint>> DShorelineMarker::findPathback(const QHash<GPoint, int>& pathsWithDepth, const GPoint& finalPoint)
{
    QSet<GPoint> path{ finalPoint };

    GPoint point = finalPoint;
    auto addToPath = [&](const GPoint& p, int expectedDepth)
    {
        if (pathsWithDepth[p] == expectedDepth)
        {
            point = p;
            path << p;
            return true;
        }
        return false;
    };

    for (int depth = pathsWithDepth[finalPoint]; depth > 1; depth--)
    {
        if (!(addToPath(GPoint(point.x, point.z + 1), depth - 1) ||
            addToPath(GPoint(point.x, point.z - 1), depth - 1) ||
            addToPath(GPoint(point.x + 1, point.z), depth - 1) ||
            addToPath(GPoint(point.x - 1, point.z), depth - 1)))
        {
            OmniLog(ELoggingLevel::Error) <<= "Couldn't find a path back - aborting";
            return {};
        }
    }

    return path;
}

std::optional<QSet<GPoint>> DShorelineMarker::findConnectingPathBetweenLandmasses(const Landmass& landmassFrom, const Landmass& landmassTo, const QSet<GPoint>& avaliableSeaside, const QHash<GPoint, std::optional<int>>& landmassIndexAffiliation, const QSet<GPoint>& illegalPoints)
{
    QHash<GPoint, int> pathsWithDepth;
    QQueue<GPoint> queue;

    auto&& addToQueue = [&](const GPoint& p, int x, int z, int depth)
    {
        const GPoint point = GPoint(p.x + x, p.z + z);

        if (pathsWithDepth[point] == 0 && (avaliableSeaside.contains(point) || landmassIndexAffiliation[point]) && !illegalPoints.contains(point))
        {
            queue.enqueue(point);
            pathsWithDepth[point] = depth + 1;
        }
    };


    for (auto&& p : landmassFrom.insideArea)
    {
        queue.enqueue(p);
        pathsWithDepth[p] = 1;
    }

    while (!queue.isEmpty())
    {
        const GPoint p = queue.dequeue();

        if (landmassTo.insideArea.contains(p))
            return findPathback(pathsWithDepth, p);

        int depth = pathsWithDepth[p];

        addToQueue(p, 1, 0, depth);
        addToQueue(p, -1, 0, depth);
        addToQueue(p, 0, 1, depth);
        addToQueue(p, 0, -1, depth);
    }

    return {};
};

QSet<int> DShorelineMarker::connectLandmasses(Landmass* landmassToConnectTo, Landmass* landmassToBeConnected, const QSet<GPoint>& pathBetweenLandmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const SquareType insideType)
{
    landmassToConnectTo->isCoast = landmassToConnectTo->isCoast || landmassToBeConnected->isCoast;

    // convert from source to target
    auto&& convertSet = [&](QSet<GPoint>& target, QSet<GPoint>& source)
    {
        target += source;
        for (auto&& p : source)
            (*landmassIndexAffiliation)[p] = landmassToConnectTo->index;
        source.clear();
    };

    convertSet(landmassToConnectTo->insideArea, landmassToBeConnected->insideArea);

    if (auto shorelineId = landmassToConnectTo->shorelineWithinSeaside(*avaliableSeaside); shorelineId)
        (*landmassToConnectTo).shorelines.erase((*landmassToConnectTo).shorelines.begin() + *shorelineId);

    QSet<int> mustBeJoinedIslands;
    for (auto&& p : pathBetweenLandmasses)
    {
        landmassToConnectTo->insideArea.insert(p);
        (*avaliableSeaside).remove(p);
        (*landmassIndexAffiliation)[p] = landmassToConnectTo->index;

        landmassToConnectTo->isCoast = landmassToConnectTo->isCoast
            || checkAccess(insideType, p, 1, 0)
            || checkAccess(insideType, p, -1, 0)
            || checkAccess(insideType, p, 0, 1)
            || checkAccess(insideType, p, 0, -1);

        for (int x = -2; x <= 2; x++)
            for (int z = -2; z <= 2; z++)
                if (auto islandIndex = (*landmassIndexAffiliation)[GPoint(p.x + x, p.z + z)]; islandIndex && landmassToConnectTo->index != islandIndex)
                    mustBeJoinedIslands.insert(*islandIndex);
    }

    return mustBeJoinedIslands;
}

bool DShorelineMarker::tryConnectingLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const SquareType insideType, const std::pair<int, int>& landmassesToConnect, const bool mustBeJoined, const bool avoidIfInApproximityToOtherLandmass)
{
    auto&& landmass1 = (*landmasses)[landmassesToConnect.first];
    auto&& landmass2 = (*landmasses)[landmassesToConnect.second];

    if (landmass1.index == landmass2.index || landmass1.insideArea.empty() || landmass2.insideArea.empty())
        return false;

    auto path = findConnectingPathBetweenLandmasses(landmass1, landmass2, *avaliableSeaside, *landmassIndexAffiliation, illegalPoints);

    if (!path && mustBeJoined)
    {
        OmniLog(ELoggingLevel::Warn) <<= "Failed to find a legal path - ignoring path conditions";
        path = findConnectingPathBetweenLandmasses(landmass1, landmass2, *avaliableSeaside, *landmassIndexAffiliation, {});
    }

    if (!path)
        return false;

    if (avoidIfInApproximityToOtherLandmass)
        for (auto&& p : *path)
            for (int x = -2; x <= 2; x++)
                for (int z = -2; z <= 2; z++)
                    if (auto landmassIndex = (*landmassIndexAffiliation)[GPoint(p.x + x, p.z + z)]; landmassIndex && (landmass1.index != landmassIndex && landmass2.index != landmassIndex))
                        return false;

    auto&& mustBeJoinedLandmasses = connectLandmasses(&landmass1, &landmass2, *path, avaliableSeaside, landmassIndexAffiliation, insideType);

    for (auto&& landmassJoin : mustBeJoinedLandmasses)
        tryConnectingLandmasses(landmasses, avaliableSeaside, landmassIndexAffiliation, illegalPoints, insideType, { landmass1.index, landmassJoin }, true, avoidIfInApproximityToOtherLandmass);

    return true;
}

void DShorelineMarker::joinTooCloseLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const SquareType insideType)
{
    QSet<GPoint> seasideCopy = *avaliableSeaside;
    for (auto&& p : seasideCopy)
    {
        QSet<int> mustBeJoinedIslands;
        for (int x = -1; x <= 1; x++)
            for (int z = -1; z <= 1; z++)
                if (auto islandIndex = (*landmassIndexAffiliation)[GPoint(p.x + x, p.z + z)]; islandIndex)
                    mustBeJoinedIslands.insert(*islandIndex);

        if (mustBeJoinedIslands.size() > 1)
            for (auto it = mustBeJoinedIslands.begin() + 1; it != mustBeJoinedIslands.end(); it++)
                tryConnectingLandmasses(landmasses, avaliableSeaside, landmassIndexAffiliation, illegalPoints, insideType, { *mustBeJoinedIslands.begin(), *it }, true, false);
    }
}

float calculateOriginIslandsExtension(const DShorelineMarker::IslandParameters& islandParams)
{
    float val1 = sqrt(std::min(islandParams.smallIslandsQuantity * 2.0f, 1.0f)) +
        sqrt(std::min(islandParams.mediumIslandsQuantity * 2.0f, 1.0f)) +
        sqrt(std::min(islandParams.largeIslandsQuantity * 2.0f, 1.0f));
    float val2 = islandParams.smallIslandsQuantity + islandParams.mediumIslandsQuantity + islandParams.largeIslandsQuantity;

    if (val2 == 0.0f)
        return 1.0f;

    float originIslandsExtension = 1.0f - 1.0f / (val1 / val2);

    return originIslandsExtension;
}

void DShorelineMarker::distributeVirtualLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const SquareType insideType, const IslandParameters& islandParams)
{
    std::vector<GVector2D> middles = findMidpointsPerLandmass(*landmasses);

    std::vector<int> virtualLandmasses;
    std::vector<int> originLandmasses;

    auto findClosestLandmasses = [&](int landmassId, bool includeOriginLandmasses = false)
    {
        std::map<float, int> distances;

        for (int i = 0; i < landmasses->size(); i++)
            if (i != landmassId && (includeOriginLandmasses || std::find(virtualLandmasses.begin(), virtualLandmasses.end(), i) != virtualLandmasses.end()))
            {
                auto dist = middles[landmassId].dist(middles[i]);
                while (distances.contains(dist))
                    dist += 0.1f;

                distances[dist] = i;
            }

        return distances;
    };

    auto connectClosestVirtualLandmasses = [&](int randomLandmass, int numberOfLandmassesToConnect, bool includeOriginLandmasses)
    {
        auto&& closestLandmasses = findClosestLandmasses(randomLandmass, includeOriginLandmasses);
        int connectedLandmassesCount = 0;
        bool connectedAnyLandmass = false;

        for (auto&& [distance, landmass] : closestLandmasses)
        {
            if (connectedLandmassesCount >= numberOfLandmassesToConnect)
                break;

            if (tryConnectingLandmasses(landmasses, avaliableSeaside, landmassIndexAffiliation, illegalPoints, insideType, { randomLandmass, landmass }, false, true))
            {
                if (auto it = std::find(virtualLandmasses.begin(), virtualLandmasses.end(), landmass); it != virtualLandmasses.end())
                    virtualLandmasses.erase(it);
                connectedLandmassesCount++;
                connectedAnyLandmass = true;
            }
        }

        return connectedAnyLandmass;
    };

    for (auto&& landmass : *landmasses)
    {
        if (landmass.insideArea.empty() || !ShorelineUtils::isNeighboring(landmass.insideArea, *avaliableSeaside))
            continue;

        // only if there is no pre existing marker on this seaside
        if (auto shorelineId = landmass.shorelineWithinSeaside(*avaliableSeaside); shorelineId && !landmass.shorelines[*shorelineId].path.empty())
            continue;

        if (!landmass.isOrigin)
            virtualLandmasses << landmass.index;
        else
            originLandmasses << landmass.index;
    }


    if (originLandmasses.empty() && !virtualLandmasses.empty() && insideType == SquareType::Terrain)
    {
        originLandmasses << virtualLandmasses.back();
        virtualLandmasses.erase(virtualLandmasses.end() - 1);
    }

    std::map<EIslandGrowType, float> islandGrowSelection({
        {EIslandGrowType::SmallIsland, islandParams.smallIslandsQuantity },
        {EIslandGrowType::MediumIsland, islandParams.mediumIslandsQuantity },
        {EIslandGrowType::LargeIsland, islandParams.largeIslandsQuantity },
        {EIslandGrowType::OriginIslandExtension, calculateOriginIslandsExtension(islandParams) },
        });

    std::vector<int> leftOutLandmasses;
    int initialVirtualLandmassesSize = virtualLandmasses.size();
    int numberOfLandmassesToDistribute = initialVirtualLandmassesSize * islandParams.coverage;

    while (true)
    {
        if (numberOfLandmassesToDistribute <= (initialVirtualLandmassesSize - (int)virtualLandmasses.size()))
            break;

        auto growType = ShorelineUtils::randomWeightedChoice(islandGrowSelection);
        int numberOfIslandsToConnect = PIslandGrowSize.at(growType);
        bool isOriginIslandExtension = growType == EIslandGrowType::OriginIslandExtension;

        int randomIsland = -1;
        if (isOriginIslandExtension)
            randomIsland = originLandmasses[std::uniform_int_distribution<int>(0, int(originLandmasses.size()) - 1)(Generation::gRandomEngine)];
        else
            randomIsland = virtualLandmasses[std::uniform_int_distribution<int>(0, int(virtualLandmasses.size()) - 1)(Generation::gRandomEngine)];

        if (numberOfIslandsToConnect > 0)
        {
            bool connectedAny = connectClosestVirtualLandmasses(randomIsland, numberOfIslandsToConnect, isOriginIslandExtension);

            if (!connectedAny && !isOriginIslandExtension)
                leftOutLandmasses << randomIsland;
        }

        if (auto it = std::find(virtualLandmasses.begin(), virtualLandmasses.end(), randomIsland); it != virtualLandmasses.end())
            virtualLandmasses.erase(it);
    }

    leftOutLandmasses.insert(leftOutLandmasses.begin(), virtualLandmasses.begin(), virtualLandmasses.end());
    for (auto&& landmass : leftOutLandmasses)
    {
        for (auto&& sq : (*landmasses)[landmass].insideArea)
        {
            (*landmassIndexAffiliation)[sq] = std::nullopt;
            (*avaliableSeaside) += sq;
        }

        (*landmasses)[landmass].insideArea.clear();
    }
}

void DShorelineMarker::joinRandomOriginLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& illegalPoints, const SquareType insideType, const IslandParameters& islandParams)
{
    static std::uniform_real_distribution<float> distribution(0, 1);
    float chanceForConnection = calculateOriginIslandsExtension(islandParams) * islandParams.coverage;

    for (int idx1 = 0; idx1 < (*landmasses).size(); idx1++)
    {
        if ((*landmasses)[idx1].insideArea.isEmpty() || !(*landmasses)[idx1].isOrigin)
            continue;

        if (auto&& shorelineId = (*landmasses)[idx1].shorelineWithinSeaside(*avaliableSeaside); shorelineId && !(*landmasses)[idx1].shorelines[*shorelineId].path.empty())
            continue;

        for (int idx2 = idx1 + 1; idx2 < (*landmasses).size(); idx2++)
        {
            if ((*landmasses)[idx2].insideArea.isEmpty() || !(*landmasses)[idx2].isOrigin)
                continue;

            if (auto&& shorelineId = (*landmasses)[idx2].shorelineWithinSeaside(*avaliableSeaside); shorelineId && !(*landmasses)[idx2].shorelines[*shorelineId].path.empty())
                continue;

            if (distribution(Generation::gRandomEngine) > chanceForConnection)
                continue;

            tryConnectingLandmasses(landmasses, avaliableSeaside, landmassIndexAffiliation, illegalPoints, insideType, { idx1, idx2 }, false, true);
        }
    }
}

void DShorelineMarker::consumeLandmassesInsideLandmasses(std::vector<Landmass>* landmasses, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation)
{
    for (auto&& landmass : (*landmasses))
    {
        if (landmass.insideArea.empty())
            continue;

        QSet<int> adjacentislands;

        auto addPoint = [&](const GPoint& p, int x, int z)
        {
            if (auto islandIndex = (*landmassIndexAffiliation)[GPoint(p.x + x, p.z + z)]; islandIndex && landmass.index != islandIndex)
                adjacentislands.insert(*islandIndex);
        };

        for (auto&& p : landmass.insideArea)
        {
            addPoint(p, 1, 0);
            addPoint(p, -1, 0);
            addPoint(p, 0, 1);
            addPoint(p, 0, -1);
        }

        if (adjacentislands.empty())
            continue;

        auto&& otherLandmass = (*landmasses)[(*adjacentislands.begin())];
        auto polygons = PolygonUtils::calculatePolygonsFromGridSquares(otherLandmass.insideArea);
        auto insidePoint = GVector2D(landmass.insideArea.begin()->x + 0.5f, landmass.insideArea.begin()->z + 0.5f) * GRID_SEGMENT_WIDTH;

        if (std::any_of(polygons.begin(), polygons.end(), [&](auto& p) { return PolygonUtils::contains(insidePoint, p); }))
        {
            OmniLog() <<= "Island inside island";

            for (auto&& p : landmass.insideArea)
                (*landmassIndexAffiliation)[p] = otherLandmass.index;

            otherLandmass.insideArea += landmass.insideArea;
            landmass.insideArea.clear();
        }
    }
}

void DShorelineMarker::resolveOutsideTypePointsInsideLandmasses(std::vector<Landmass>* landmasses, QSet<GPoint>* avaliableSeaside, QHash<GPoint, std::optional<int>>* landmassIndexAffiliation, const QSet<GPoint>& outsideTypePoints, const SquareType insideType)
{
    QSet<GPoint> landmassesInsides;
    for (auto&& landmass : *landmasses)
        for (auto&& sq : landmass.insideArea)
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    if (getSquareType(sq, x, z) == SquareType::Seaside)
                        landmassesInsides += GPoint(sq.x + x, sq.z + z);

    QSet<GPoint> unassignedSeasides;
    for (auto&& p : *avaliableSeaside)
        if (!(*landmassIndexAffiliation)[p])
            unassignedSeasides += p;
    unassignedSeasides -= landmassesInsides;

    auto outsideTypeSets = ShorelineUtils::splitSeparateSet(outsideTypePoints + unassignedSeasides);

    for (auto&& outsideTypeSet : outsideTypeSets)
        if (std::all_of(outsideTypeSet.begin(), outsideTypeSet.end(), [&](auto& p) { return !outsideTypePoints.contains(p); }))
            outsideTypeSet.clear();

    for (auto&& outsideTypeSet : outsideTypeSets)
    {
        auto surroundingLandmassIndex = findLandmassSurroundingSet(outsideTypeSet, *landmassIndexAffiliation);

        if (!surroundingLandmassIndex)
            continue;

        auto&& landmass = (*landmasses)[*surroundingLandmassIndex];
        for(auto&& sq : outsideTypeSet)
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    if (getSquareType(sq, x, z) == SquareType::Seaside)
                        landmass.insideArea += GPoint(sq.x + x, sq.z + z);
    }
}

void DShorelineMarker::assignMustHaveShorelineAreas(std::vector<Landmass>* landmasses, const QSet<GPoint>& avaliableSeaside, const QSet<GPoint>& illegalPoints)
{
    for (auto&& landmass : (*landmasses))
        assignMustHaveShorelineAreas(&landmass, avaliableSeaside, illegalPoints);
}

void DShorelineMarker::assignMustHaveShorelineAreas(Landmass* landmass, const QSet<GPoint>& avaliableSeaside, const QSet<GPoint>& illegalPoints)
{
    if (!landmass->shorelineWithinSeaside(avaliableSeaside))
    {
        QSet<GPoint> shorelineAreas;

        for (auto&& p : landmass->insideArea)
            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    if (auto sq = GPoint(p.x + x, p.z + z); avaliableSeaside.contains(sq) && !landmass->insideArea.contains(sq) && !illegalPoints.contains(sq))
                        shorelineAreas += sq;

        for (auto&& shorelineArea : ShorelineUtils::splitSeparateSet(shorelineAreas))
            landmass->shorelines.push_back(Shoreline{ .area = shorelineArea, .path = {} });
    }
}

void DShorelineMarker::assignBonusSeasidePointsToLandmasses(std::vector<Landmass>* landmasses, const QSet<GPoint>& avaliableSeaside, const QSet<GPoint>& illegalPoints, const float sizeParam)
{
    QSet<GPoint> avaliableSeasideWithoutShorelines = avaliableSeaside;
    avaliableSeasideWithoutShorelines -= illegalPoints;

    for (auto&& landmass : *landmasses)
        for (auto&& shoreline : landmass.shorelines)
            avaliableSeasideWithoutShorelines -= shoreline.area;

    float numberOfShorelinesOnSeaside = 0;
    for (auto&& landmass : *landmasses)
        if (landmass.shorelineWithinSeaside(avaliableSeaside))
            numberOfShorelinesOnSeaside++;

    float seasideSize = avaliableSeasideWithoutShorelines.size();

    for (auto&& landmass : *landmasses)
    {
        for (auto&& shoreline : landmass.shorelines)
        {
            int shorelineMaxIncrease = (seasideSize / numberOfShorelinesOnSeaside) * sizeParam;
            int currentShorelineIncrease = 0;

            QQueue<GPoint> queue;

            auto&& addPoint = [&](const GPoint& p, int x, int z)
            {
                if (currentShorelineIncrease > shorelineMaxIncrease)
                    return;

                const GPoint point = GPoint(p.x + x, p.z + z);

                if (!avaliableSeasideWithoutShorelines.contains(point))
                    return;

                shoreline.area += point;
                currentShorelineIncrease++;
                avaliableSeasideWithoutShorelines -= point;
                queue.enqueue(point);
            };

            for (auto&& sq : shoreline.area)
                queue.enqueue(sq);

            while (true)
            {
                if (currentShorelineIncrease > shorelineMaxIncrease || queue.isEmpty())
                    break;

                const GPoint point = queue.dequeue();
                addPoint(point, 1, 0);
                addPoint(point, -1, 0);
                addPoint(point, 0, 1);
                addPoint(point, 0, -1);
            }
        }
    }
}

void DShorelineMarker::detachedPoints(const QSet<GPoint>& island, QSet<GPoint>* seaside, const SquareType insideType, QSet<GPoint>* sourceSeaside, QSet<GPoint>* otherSeaside)
{
    QSet<GPoint> seasidePointsLeft = (*sourceSeaside);
    auto&& checkPoint = [&](const GPoint& p, int x, int z, bool* terrain, bool* water, bool* otherSeasideAccess)
    {
        const SquareType pointType = getSquareType(p, x, z);
        const GPoint point = GPoint(p.x + x, p.z + z);

        if (!(*seaside).contains(point))
        {
            if (pointType == SquareType::Terrain)
                (*terrain) = true;
            else if (pointType == SquareType::Water)
                (*water) = true;
            else
            {
                if (island.contains(point))
                    if (insideType == SquareType::Water)
                        (*water) = true;
                    else
                        (*terrain) = true;
                else
                    if (insideType == SquareType::Water)
                        (*terrain) = true;
                    else
                        (*water) = true;
            }
        }
        else if ((*otherSeaside).contains(point))
            (*otherSeasideAccess) = true;
    };

    while (!seasidePointsLeft.isEmpty())
    {
        QSet<GPoint> justVisited = ShorelineUtils::coverage(seasidePointsLeft, *seasidePointsLeft.cbegin());

        bool pureTerrain = false;
        bool pureWater = false;
        bool otherSeasideAccess = false;
        for (auto&& p : justVisited)
        {
            checkPoint(p, 1, 0, &pureTerrain, &pureWater, &otherSeasideAccess);
            checkPoint(p, -1, 0, &pureTerrain, &pureWater, &otherSeasideAccess);
            checkPoint(p, 0, 1, &pureTerrain, &pureWater, &otherSeasideAccess);
            checkPoint(p, 0, -1, &pureTerrain, &pureWater, &otherSeasideAccess);
        }

        if (!pureTerrain || !pureWater)
        {
            (*sourceSeaside) -= justVisited;
            if (otherSeasideAccess)
                (*otherSeaside) += justVisited;
            else
                (*seaside) -= justVisited;
        }

        seasidePointsLeft -= justVisited;
    }
}

QVector3D DShorelineMarker::classifySeasidePoint(const QSet<GPoint>& island, const QSet<GPoint>& seaside, const QSet<GPoint>& seaside1, const QSet<GPoint>& seaside2, const GPoint point)
{
    if (!seaside.contains(point) && !island.contains(point))
        return QVector3D(1, 0, 0);
    if (seaside1.contains(point))
        return QVector3D(0, 1, 0);
    if (seaside2.contains(point))
        return QVector3D(0, 0, 1);
    return QVector3D(0, 0, 0);
}

QSet<GPoint> DShorelineMarker::generatePotentialEndpoints(const QSet<GPoint>& island, const QSet<GPoint>& seaside, const SquareType insideType, const QSet<GPoint>& seaside1, const QSet<GPoint>& seaside2)
{
    QSet<GPoint> potentialEndpoints;

    for (auto&& sq : seaside)
    {
        /* We consider this point as a potential endpoint.
         * In case 1 and 2 we consider sq as upper left point of a 4 point square.
         * In case 3 we consider sq as bottom right.
         * In case 4 we consider both options.
         * Case 1 : (0 nulls, 2 seaside1 points, 2 seaside2 points)
         * Case 2 : (2 nulls, 1 s1s, 1 s2s)
         * Case 3 : (2 nulls, 1 s1s, 1 s2s) - we consider sq as bottom right point though
         * Case 4 : (1 null, 1 s1s, 2 s2s) or (1 null, 2 s1s, 1s1s) - edge case for bottom right or upper left
         * Case 5 : (0 nulls, 3 s1s, 1 s2s)
         * Case 6 : (0 nulls, 1 s1s, 3 s2s) */

        QVector3D caseUL, caseBR = QVector3D(); // This is not meant to be a representation on the grid in any way.

        // Case 1 & 2 - consider [x, z], [x + 1, z], [x, z - 1], [x + 1, z - 1]  
        caseUL = classifySeasidePoint(island, seaside, seaside1, seaside2, sq);
        caseUL += classifySeasidePoint(island, seaside, seaside1, seaside2, GPoint(sq.x + 1, sq.z));
        caseUL += classifySeasidePoint(island, seaside, seaside1, seaside2, GPoint(sq.x, sq.z - 1));
        caseUL += classifySeasidePoint(island, seaside, seaside1, seaside2, GPoint(sq.x + 1, sq.z - 1));

        // Case 3 - consider [x, z], [x - 1, z], [x, z + 1], [x - 1, z + 1]
        caseBR = classifySeasidePoint(island, seaside, seaside1, seaside2, sq);
        caseBR += classifySeasidePoint(island, seaside, seaside1, seaside2, GPoint(sq.x - 1, sq.z));
        caseBR += classifySeasidePoint(island, seaside, seaside1, seaside2, GPoint(sq.x, sq.z + 1));
        caseBR += classifySeasidePoint(island, seaside, seaside1, seaside2, GPoint(sq.x - 1, sq.z + 1));

        // In case 1 and 2 where we consider sq as upper left, the center of the 4-point square will be [x + 1, z]
        if (caseUL == QVector3D(2, 1, 1) || caseUL == QVector3D(0, 2, 2) || caseUL == QVector3D(1, 2, 1) || caseUL == QVector3D(1, 1, 2) || caseUL == QVector3D(0, 3, 1) || caseUL == QVector3D(0, 1, 3))
            potentialEndpoints.insert(GPoint(sq.x + 1, sq.z));

        // In case 3 where we consider sq as bottom right, the center of the 4-point square will be [x, z + 1]
        if (caseBR == QVector3D(2, 1, 1) || caseBR == QVector3D(1, 2, 1) || caseBR == QVector3D(1, 1, 2))
            potentialEndpoints.insert(GPoint(sq.x, sq.z + 1));
    }

    return potentialEndpoints;
}

std::tuple<std::vector<GPoint>, std::vector<GPoint>> DShorelineMarker::dividePotentialEndpoints(QSet<GPoint>* potentialEndpoints)
{
    std::vector<GPoint> potentialLeftEndpoints;
    std::vector<GPoint> potentialRightEndpoints;

    GPoint randomEndpoint = *(*potentialEndpoints).begin();
    GPoint firstEndpoint;
    GPoint secondEndpoint;

    auto&& distSq = [](const GPoint& a, const GPoint& b)
    {
        return pow(a.x - b.x, 2) + pow(a.z - b.z, 2);
    };

    auto&& farthestPoint = [&potentialEndpoints, &distSq](const GPoint& basePoint, GPoint* result)
    {
        int dist = 0;
        for (auto&& p : (*potentialEndpoints))
        {
            int x = distSq(basePoint, p);
            if (x >= dist)
            {
                dist = x;
                *result = p;
            }
        }
    };

    farthestPoint(randomEndpoint, &firstEndpoint);
    farthestPoint(firstEndpoint, &secondEndpoint);

    auto&& qualifyEndpoints = [&potentialEndpoints](const GPoint& source, std::vector<GPoint>* endpoints)
    {
        QQueue<GPoint> queue;

        auto&& addPotentialEndpoint = [&](const GPoint& p, int x, int z)
        {
            const GPoint point = GPoint(p.x + x, p.z + z);
            if ((*potentialEndpoints).contains(point))
            {
                (*potentialEndpoints).remove(point);
                (*endpoints) << point;
                queue.enqueue(point);
            }
        };

        addPotentialEndpoint(source, 0, 0);

        while (!queue.isEmpty())
        {
            const GPoint point = queue.dequeue();

            for (int x = -1; x <= 1; x++)
                for (int z = -1; z <= 1; z++)
                    if (!(x == 0 && z == 0))
                        addPotentialEndpoint(point, x, z);
        }
    };

    qualifyEndpoints(firstEndpoint, &potentialLeftEndpoints);
    qualifyEndpoints(secondEndpoint, &potentialRightEndpoints);
    (*potentialEndpoints).clear();
    return { potentialLeftEndpoints, potentialRightEndpoints };
}


std::tuple<bool, bool, QSet<GPoint> > DShorelineMarker::checkSeasideForEndpointAccess(const std::vector<GPoint>& potentialLeftEndpoints, const std::vector<GPoint>& potentialRightEndpoints, QSet<GPoint>* sourceSeaside, const GPoint& root, const GPoint excludedP = GPoint(-1, -1))
{
    QQueue<GPoint> queue;
    QSet<GPoint> visited;
    bool leftEndpointAccess = false;
    bool rightEndpointAccess = false;

    auto&& addToQueue = [&](const GPoint& p, int x, int z)
    {
        const GPoint point = GPoint(p.x + x, p.z + z);

        if (!visited.contains(point) && (*sourceSeaside).contains(point) && point != excludedP)
        {
            visited.insert(point);
            queue.enqueue(point);
        }
    };

    auto&& checkEndpointAccess = [&](const GPoint& p, int x, int z)
    {
        const GPoint point = GPoint(p.x + x, p.z + z);
        if (contains(potentialLeftEndpoints, point))
            leftEndpointAccess = true;
        if (contains(potentialRightEndpoints, point))
            rightEndpointAccess = true;

    };

    addToQueue(root, 0, 0);
    checkEndpointAccess(root, 0, 0);

    while (!queue.isEmpty())
    {
        const GPoint point = queue.dequeue();
        addToQueue(point, 1, 0);
        addToQueue(point, -1, 0);
        addToQueue(point, 0, 1);
        addToQueue(point, 0, -1);

        for (int x = -1; x <= 1; x++)
            for (int z = -1; z <= 1; z++)
                checkEndpointAccess(point, x, z);
    }

    return { leftEndpointAccess, rightEndpointAccess, visited };
}

void DShorelineMarker::convertSeasidesBasedOnEndpoints(const QSet<GPoint> seaside, const std::vector<GPoint>& potentialLeftEndpoints, const std::vector<GPoint>& potentialRightEndpoints, QSet<GPoint>* seaside1, QSet<GPoint>* seaside2)
{
    QSet<GPoint> visited;

    auto&& checkSeaside = [&](QSet<GPoint>* sourceSeaside, QSet<GPoint>* targetSeaside, const GPoint& p)
    {
        auto [left, right, justVisited] = checkSeasideForEndpointAccess(potentialLeftEndpoints, potentialRightEndpoints, sourceSeaside, p);

        bool endpointAccess = left || right;

        visited += justVisited;

        if (!endpointAccess)
        {
            (*sourceSeaside) -= justVisited;
            (*targetSeaside) += justVisited;
        }
    };

    for (auto&& p : seaside)
    {
        if (!visited.contains(p))
        {
            if ((*seaside1).contains(p))
            {
                checkSeaside(seaside1, seaside2, p);
            }
            else
            {
                checkSeaside(seaside2, seaside1, p);
            }
        }
    }

}

/* Erase bad elements.
        * For the given endpoint we know that is the upper right point of the 4-point square as described in addPotentialEndpoint, as it must be a point from Case 1 (Case 2 and Case 3 always work correctly).
        * We consider [x, z], [x - 1, z], [x, z - 1], [z - 1, z - 1].
        * If the 4-point square passes this, then we proceed to remove a line of seaside points from the endpoint to the seaside-water border. */
void DShorelineMarker::removeDangerousPoints(const QSet<GPoint>& island, const QSet<GPoint>& seaside, QSet<GPoint>* seaside1, QSet<GPoint>* seaside2, const GPoint& endpoint, const std::vector<GPoint>& potentialLeftEndpoints, const std::vector<GPoint>& potentialRightEndpoints)
{
    QVector3D vec;

    vec = classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), endpoint);
    vec += classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x - 1, endpoint.z));
    vec += classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x, endpoint.z - 1));
    vec += classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x - 1, endpoint.z - 1));
    if (vec != QVector3D(0, 2, 2) && vec != QVector3D(1, 2, 1) && vec != QVector3D(1, 1, 2) && vec != QVector3D(0, 1, 3) && vec != QVector3D(0, 3, 1)) // If the point is invalid then we don't need to delete anything.
        return;

    auto&& removeAppropriate = [&](GPoint p1, GPoint p2)
    {
        if (!((*seaside1).contains(p1) || (*seaside2).contains(p1))
            || !((*seaside1).contains(p2) || (*seaside2).contains(p2)))
            return;

        QSet<GPoint>* seaside;

        if ((*seaside1).contains(p1))
            seaside = seaside1;
        else
            seaside = seaside2;

        auto [leftP1, rightP1, visitedP1] = checkSeasideForEndpointAccess(potentialLeftEndpoints, potentialRightEndpoints, seaside, p1, p2);
        auto [leftP2, rightP2, visitedP2] = checkSeasideForEndpointAccess(potentialLeftEndpoints, potentialRightEndpoints, seaside, p2, p1);

        // leftP and rightP means that a point has access to both endpoints.

        if (visitedP1.size() >= visitedP2.size() && leftP1 && rightP1)
        {
            (*seaside).remove(p2);
            return;
        }

        if (visitedP1.size() <= visitedP2.size() && leftP2 && rightP2)
        {
            (*seaside).remove(p1);
            return;
        }

        if (leftP1 && rightP1)
        {
            (*seaside).remove(p2);
            return;
        }

        if (leftP2 && rightP2)
        {
            (*seaside).remove(p1);
            return;
        }

        OmniLog(ELoggingLevel::Error) << "Couldn't remove a bad point" <<= "";
    };

    // Askew points
    if (classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), endpoint) == classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x - 1, endpoint.z - 1)))
        removeAppropriate(endpoint, GPoint(endpoint.x - 1, endpoint.z - 1));

    if (classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x - 1, endpoint.z)) == classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x, endpoint.z - 1)))
        removeAppropriate(GPoint(endpoint.x - 1, endpoint.z), GPoint(endpoint.x, endpoint.z - 1));

    // Vertical / horizontal points
    if (classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), endpoint) == classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x, endpoint.z - 1)))
        removeAppropriate(endpoint, GPoint(endpoint.x, endpoint.z - 1));

    if (classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x - 1, endpoint.z - 1)) == classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x - 1, endpoint.z)))
        removeAppropriate(GPoint(endpoint.x - 1, endpoint.z - 1), GPoint(endpoint.x - 1, endpoint.z));

    if (classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint)) == classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x - 1, endpoint.z)))
        removeAppropriate(endpoint, GPoint(endpoint.x - 1, endpoint.z));

    if (classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x - 1, endpoint.z - 1)) == classifySeasidePoint(island, seaside, (*seaside1), (*seaside2), GPoint(endpoint.x, endpoint.z - 1)))
        removeAppropriate(GPoint(endpoint.x - 1, endpoint.z - 1), GPoint(endpoint.x, endpoint.z - 1));
}

void DShorelineMarker::swapEndpointsIfNeeded(const SquareType insideType, GVector2D* lp, GVector2D* rp)
{
    if ((*lp).x > (*rp).x)
        std::swap((*lp), (*rp));

    if (insideType == SquareType::Water)
        std::swap((*lp), (*rp));
}

QSharedPointer<DLandmassMarker> DShorelineMarker::generateLandmassMarker(const Landmass& landmass)
{
    std::vector<QSharedPointer<DShorelineMarker>> shorelineMarkers;
    std::vector<QSharedPointer<DShorelineMarker>> innerSeaShorelineMarkers;

    for (auto&& shoreline : landmass.shorelines)
        shorelineMarkers << spawn<DShorelineMarker>(shoreline.path, !landmass.isCoast);

    for (auto&& shoreline : landmass.innerSeaShorelines)
        innerSeaShorelineMarkers << spawn<DShorelineMarker>(shoreline.path, true);

    QSharedPointer<DLandmassMarker> landmassMarker; 

    landmassMarker = spawn<DLandmassMarker>(shorelineMarkers, innerSeaShorelineMarkers);

    for (auto&& shorelineMarker : shorelineMarkers)
        shorelineMarker->setLandmass(landmassMarker);

    for (auto&& shorelineMarker : innerSeaShorelineMarkers)
        shorelineMarker->setLandmass(landmassMarker);

    return landmassMarker;
}

std::vector<QSharedPointer<DLandmassMarker>> DShorelineMarker::generateLandmassesMarkers(const std::vector<Landmass>& landmasses)
{
    OmniProfile("Generating landmass markers");

    std::vector<QSharedPointer<DLandmassMarker>> markers;

    for (auto&& landmass : landmasses)
        markers << generateLandmassMarker(landmass);

    return markers;
}

QSet<GPoint> DShorelineMarker::findPerimeterPoints(const QSet<GPoint>& set)
{
    QSet<GPoint> result;

    for (auto&& p : set)
    {
        for (int x = -1; x <= 1; x++)
            for (int z = -1; z <= 1; z++)
                if (!set.contains(GPoint(p.x + x, p.z + z)) && getSquareType(p, x, z) == SquareType::Seaside)
                    result += p;
    }

    return result;
}

QHash<GPoint, std::optional<int>> computeLandmassIndexAffiliation(const std::vector<Landmass>& landmasses)
{
    // gpoint -> landmass index
    QHash<GPoint, std::optional<int>> landmassIndexAffiliation;

    for (auto&& landmass : landmasses)
        for (auto&& sq : landmass.insideArea)
            landmassIndexAffiliation[sq] = landmass.index;

    return landmassIndexAffiliation;
}

void DShorelineMarker::generateRandomLandmassAreas(std::vector<Landmass>* landmasses, QSet<GPoint> avaliableSeaside, const IslandParameters& params, SquareType insideType, bool treatPerimeterAsOutsideBounds /*= false*/)
{
    Q_ASSERT(insideType == SquareType::Terrain || insideType == SquareType::Water);

    OmniProfile("Generate random landmass areas");

    auto&& [outsideTypePoints, outsideTypeBoundingPoints] = findOutsideTypePoints(avaliableSeaside, insideType);
    for_each_sq_neighbor<true>(avaliableSeaside, [&](auto&& sq, auto&& n_sq) 
        {
            if (!avaliableSeaside.contains(n_sq) && getSquareType(n_sq) == SquareType::Seaside)
                outsideTypeBoundingPoints += sq;
        });

    if (treatPerimeterAsOutsideBounds)
        outsideTypeBoundingPoints += findPerimeterPoints(avaliableSeaside);

    for (int i = 0; i < landmasses->size(); i++)
        (*landmasses)[i].index = i;

    // gpoint -> island index
    auto&& landmassIndexAffiliation = computeLandmassIndexAffiliation(*landmasses);

    populateSeasideWithVirtualLandmasses(landmasses, &avaliableSeaside, &landmassIndexAffiliation, outsideTypeBoundingPoints, params);

    distributeVirtualLandmasses(landmasses, &avaliableSeaside, &landmassIndexAffiliation, outsideTypeBoundingPoints, insideType, params);

    joinRandomOriginLandmasses(landmasses, &avaliableSeaside, &landmassIndexAffiliation, outsideTypeBoundingPoints, insideType, params);

    consumeLandmassesInsideLandmasses(landmasses, &landmassIndexAffiliation);

    if (insideType == SquareType::Terrain)
        resolveOutsideTypePointsInsideLandmasses(landmasses, &avaliableSeaside, &landmassIndexAffiliation, outsideTypePoints, insideType);

    assignMustHaveShorelineAreas(landmasses, avaliableSeaside, outsideTypePoints);

    assignBonusSeasidePointsToLandmasses(landmasses, avaliableSeaside, outsideTypeBoundingPoints, params.coverage);

    landmasses->erase(std::remove_if(landmasses->begin(), landmasses->end(),[](auto& l) { return l.insideArea.empty(); }), landmasses->end());
}

void DShorelineMarker::generateShorelineMarkers(Landmass* landmass, const IslandParameters& params, const SquareType& insideType)
{
    OmniProfile("Generate Shorelines");

    tbb::parallel_for(0, int(landmass->shorelines.size()), [&](int idx)
        {
            auto&& shoreline = landmass->shorelines[idx];

            Q_ASSERT(!landmass->insideArea.empty() && !shoreline.area.empty());

            if (!shoreline.path.empty())
                return;

            // lazy approach, should be generalized later
            //if (island.insidePoints.size() <= 1 && island.seasidePoints.empty() && insideType == SquareType::Terrain)
            //{
            //    auto&& onlySquare = !island.insidePoints.empty() ? island.insidePoints : island.seasidePoints;
            //    generateCoastShorelines(&shorelines, onlySquare, onlySquare, params, insideType);
            //}
            std::vector<QVector3D> shorelinePath;

            if (landmass->isCoast)
                shorelinePath = createCoastShorelinePath(landmass->insideArea, shoreline.area, params, insideType);
            else
                shorelinePath = createIslandShorelinePath(landmass->insideArea, shoreline.area, params, insideType);

            // Path of inner sea is reverse to paths of land (it is calibrated according to it's inside)
            if (insideType == SquareType::Water)
                std::reverse(shorelinePath.begin(), shorelinePath.end());

            shoreline.path = shorelinePath;

            shoreline.area.clear();
            for (auto it = std::next(shorelinePath.begin()); it != std::prev(shorelinePath.end()); it++)
                shoreline.area += ((GVector2D)*it).toGPoint();
        });

    updateLandmassAreas(landmass);
}


void DShorelineMarker::updateLandmassAreas(Landmass* landmass)
{
    QSet<GPoint> shorelineAreas;

    for (auto&& shoreline : landmass->shorelines)
        shorelineAreas += shoreline.area;

    for (auto&& shoreline : landmass->innerSeaShorelines)
        shorelineAreas += shoreline.area;

    QQueue<GPoint> queue;

    auto&& addToQueue = [&](const GPoint& p, int x, int z)
    {
        const GPoint point = GPoint(p.x + x, p.z + z);

        if (!shorelineAreas.contains(point) && !landmass->insideArea.contains(point) && getSquareType(point) == SquareType::Seaside)
        {
            landmass->insideArea += point;
            queue.push_back(point);
        }
    };

    for (auto&& p : landmass->insideArea)
        queue << p;

    while (!queue.isEmpty())
    {
        const GPoint p = queue.dequeue();
        addToQueue(p, 0, 1);
        addToQueue(p, 1, 0);
        addToQueue(p, 0, -1);
        addToQueue(p, -1, 0);
    }
}


std::vector<QVector3D> DShorelineMarker::createCoastShorelinePath(const QSet<GPoint>& land, const QSet<GPoint>& seaside, const IslandParameters& params, SquareType insideType)
{
    static auto&& createRandomNeighborPoint = [](const GPoint& gp)
    {
        auto randomPointOffset = std::uniform_int_distribution<int>(0, 1)(Generation::gRandomEngine) % 2 ? 1 : -1;
        auto randomAxis = (bool)std::uniform_int_distribution<int>(0, 1)(Generation::gRandomEngine);

        return GPoint(gp.x + (randomAxis ? 0 : randomPointOffset), gp.z + (randomAxis ? randomPointOffset : 0));
    };

    std::optional<QSet<GPoint>> fakeOuterLand;
    if (seaside.size() == 1 && land.size() == 1 && *seaside.begin() == *land.begin())
        fakeOuterLand = { createRandomNeighborPoint(*land.begin()) };

    auto endPts = findCoastEndpoints(seaside, fakeOuterLand ? *fakeOuterLand : land);
    if (!endPts)
        return {};

    Q_ASSERT(endPts);

    return createCoastlinePath(insideType, seaside, land, endPts->first, endPts->second, params.shorelineComplexity);
}

std::vector<QVector3D> DShorelineMarker::createIslandShorelinePath(const QSet<GPoint>& land, const QSet<GPoint>& seaside, const IslandParameters& params, SquareType insideType)
{
    static auto&& generate2DVector = [](GPoint coords)
    {
        return GVector2D(coords.x + 0.5f, coords.z + 0.5f) * GRID_SEGMENT_WIDTH;
    };

    // Split our seaside in two based on a vector and the middlePoint.
    // Full randomness of splitting
    // Ensure X is > 0 for proper DD generation
    float randomX = std::uniform_int_distribution<int>(0, 1000)(Generation::gRandomEngine) + 0.1f;
    float randomZ = std::uniform_int_distribution<int>(-1000, 1000)(Generation::gRandomEngine) + 0.1f;

    auto&& baseV = QVector3D(randomX, 0, randomZ).normalized();

    QSet<GPoint> seasideCopy = seaside;

    auto landMidPoint = ShorelineUtils::findMidpoint(land).midPoint();;
    auto [seaside1, seaside2] = ShorelineUtils::splitSetByVector(seasideCopy, baseV, landMidPoint);

    // Seaside1 bad points
    detachedPoints(land, &seasideCopy, insideType, &seaside1, &seaside2);

    // Seaside2 bad points
    detachedPoints(land, &seasideCopy, insideType, &seaside2, &seaside1);

    QSet<GPoint> potentialEndpoints = generatePotentialEndpoints(land, seasideCopy, insideType, seaside1, seaside2);

    // Those sets will contain potential endpoints - one point from each set will be passed into arguments of computeSquigPath_LongestOptimal
    auto [potentialLeftEndpoints, potentialRightEndpoints] = dividePotentialEndpoints(&potentialEndpoints);

    convertSeasidesBasedOnEndpoints(seasideCopy, potentialLeftEndpoints, potentialRightEndpoints, &seaside1, &seaside2);

    Q_ASSERT(!potentialLeftEndpoints.empty() && !potentialRightEndpoints.empty());

    // The following logic chooses random endpoints.
    const int lIdx = std::uniform_int_distribution<int>(0, int(potentialLeftEndpoints.size()) - 1)(Generation::gRandomEngine);
    auto&& lp = potentialLeftEndpoints[lIdx];

    const int rIdx = std::uniform_int_distribution<int>(0, int(potentialRightEndpoints.size()) - 1)(Generation::gRandomEngine);
    auto&& rp = potentialRightEndpoints[rIdx];

    auto&& leftEndpoint = GVector2D(lp.x, lp.z) * GRID_SEGMENT_WIDTH;
    auto&& rightEndpoint = GVector2D(rp.x, rp.z) * GRID_SEGMENT_WIDTH;

    // Not important, cause bugs? Delete if no releted bugs are found
    //removeDangerousPoints(land, seasideCopy, &seaside1, &seaside2, lp, potentialLeftEndpoints, potentialRightEndpoints);
    //removeDangerousPoints(land, seasideCopy, &seaside1, &seaside2, rp, potentialLeftEndpoints, potentialRightEndpoints);
    swapEndpointsIfNeeded(insideType, &leftEndpoint, &rightEndpoint);

    return createIslandPath(insideType, seaside1, seaside2, leftEndpoint, rightEndpoint, params.shorelineComplexity);
}

std::vector<QVector3D> DShorelineMarker::createCoastlinePath(const SquareType& insideType, const QSet<GPoint>& seaside, const QSet<GPoint>& land, const GVector2D& p1, const GVector2D& p2, float complexity)
{
    std::vector<QSharedPointer<Generation::SquigSquare>> path;
    Generation::SquigArea area(ShorelineUtils::splitSetIntoSquares(seaside, 1), GRID_SEGMENT_WIDTH, complexity);
    path = area.computeSquigPath(5, p1, p2).path;

    if (path.size() == 0)
    {
        OmniLog(ELoggingLevel::Error) <<= "Couldn't generate shoreline";
        return {};
    }

    std::vector<QVector3D> shorelinePath;

    for (auto&& sq : path)
        shorelinePath << sq->getMidpoint();

    // Ensure the sea is to the left.
    GPoint gpCoords = GPoint(std::round(shorelinePath[0].x() / GRID_SEGMENT_WIDTH), 
                             std::round(shorelinePath[0].z() / GRID_SEGMENT_WIDTH));

    //Must be of length 1.
    auto&& getInlandVector = [&](int x, int z, QVector3D* result)
    {
        GVector2D pCoords(gpCoords.x * GRID_SEGMENT_WIDTH + x * GRID_SEGMENT_WIDTH * 0.5f,
                          gpCoords.z * GRID_SEGMENT_WIDTH + z * GRID_SEGMENT_WIDTH * 0.5f);

        if (result->isNull() && std::any_of(land.begin(), land.end(), [&](auto& l) { return l.contains(pCoords); })
            && std::any_of(seaside.begin(), seaside.end(), [&](auto& s) { return s.contains(pCoords); }))
            *result = { float(x), 0, float(z) };
    };

    QVector3D inlandVector;
    getInlandVector(1, 0, &inlandVector);
    getInlandVector(0, 1, &inlandVector);
    getInlandVector(-1, 0, &inlandVector);
    getInlandVector(0, -1, &inlandVector);

    // Get a shore directional vector that is not grid-aligned.
    QVector3D* target = &shorelinePath[0];
    while ((target->x() == shorelinePath[0].x()) || (target->z() == shorelinePath[0].z()))
        ++target;

    QVector3D leftOfShore = *target - shorelinePath[0];
    leftOfShore = QQuaternion::fromEulerAngles(0, 90.0f, 0).rotatedVector(leftOfShore).normalized();

    shorelinePath.insert(shorelinePath.begin(), p1);
    shorelinePath << p2;

    // leftOfShore should more than 90* away from inlandVector
    if (QVector3D::dotProduct(leftOfShore, inlandVector) > 0.0f)
        std::reverse(shorelinePath.begin(), shorelinePath.end());

    return shorelinePath;
}

std::vector<QVector3D> DShorelineMarker::createIslandPath(const SquareType& insideType, const QSet<GPoint>& seaside1, const QSet<GPoint>& seaside2, const GVector2D& leftEndpoint, const GVector2D& rightEndpoint, float complexity)
{
    std::vector<QSharedPointer<Generation::SquigSquare>> path;
    Generation::SquigArea area1(ShorelineUtils::splitSetIntoSquares(seaside1, 1), GRID_SEGMENT_WIDTH, complexity);
    auto path1 = area1.computeSquigPath(5, leftEndpoint, rightEndpoint).path;

    Generation::SquigArea area2(ShorelineUtils::splitSetIntoSquares(seaside2, 1), GRID_SEGMENT_WIDTH, complexity);
    auto path2 = area2.computeSquigPath(5, rightEndpoint, leftEndpoint).path;

    if (path1.size() == 0 || path2.size() == 0)
    {
        OmniLog(ELoggingLevel::Error) << "Failed to generate path";
        return {};
    }

    path << path1 << path2;

    std::vector<QVector3D> shorelinePath;

    for (auto&& sq : path)
        shorelinePath << sq->getMidpoint();

    return shorelinePath;
}

std::optional<int> Landmass::shorelineWithinSeaside(const QSet<GPoint>& seaside)
{
    for (int i = 0; i < shorelines.size(); i++)
        for (auto&& sq : shorelines[i].area)
            if (seaside.contains(sq))
                return i;

    return {};
}

std::optional<int> Landmass::innerSeaShorelineWithinSeaside(const QSet<GPoint>& seaside)
{
    for (int i = 0; i < innerSeaShorelines.size(); i++)
        for (auto&& sq : innerSeaShorelines[i].area)
            if (seaside.contains(sq))
                return i;

    return {};
}

std::vector<QPair<QSharedPointer<BayNode>, QSharedPointer<BayNode>>> BayNode::getNeighboringSubnodes() const
{
    std::vector<QSharedPointer<BayNode>> childrenSorted = children;
    std::sort(childrenSorted.begin(), childrenSorted.end(), [](const QSharedPointer<BayNode>& A, const QSharedPointer<BayNode>& B) { return A->range < B->range; });

    std::vector<QPair<QSharedPointer<BayNode>, QSharedPointer<BayNode>>> result;

    for (int i = 0; i < int(childrenSorted.size()) - 1; ++i)
        if (childrenSorted[i]->range.second <= childrenSorted[i + 1]->range.first)
            result << QPair{childrenSorted[i], childrenSorted[i + 1]};

    return result;
}

bool BayNode::contains(QSharedPointer<BayNode> node) const
{
    return (node->range.first > range.first) && (node->range.second < range.second);
}

bool BayNode::contains(int point) const
{
    return (point >= range.first) && (point <= range.second);
}

bool BayNode::insert(QSharedPointer<BayNode> node)
{
    if (!contains(node))
        return false;

    for (auto&& child : children)
        if (child->insert(node))
            return true;

    children << node;
    node->parent = sharedFromThis();

    return true;
}
