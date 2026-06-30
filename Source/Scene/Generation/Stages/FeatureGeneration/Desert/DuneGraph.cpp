#include "stdafx.h"
#include "DuneGraph.h"

#include "Scene/Generation/Stages/FeatureGeneration/TerrainBlockUtils.h"
#include "Scene/Generation/Stages/FeatureGeneration/Desert/TerrainBlockDesert.h"
#include <Mathematics/IntpThinPlateSpline2.h>
#include <tbb/parallel_for.h>

#define DEBUG_RIDGES 1


static float getDistanceToCurve(const std::vector<GVector2D>& bounds, const GVector2D& p)
{
    const auto [_1, dist, _2] = directionalBoundDistance(bounds, p);
    return dist;
}


std::array<Polygon2D, 2> DuneFace::getPolygons(int curvePointsCount) const
{
    std::vector<GVector2D> bottomPts = bottomCurve.curve.getPoints(curvePointsCount);
    std::vector<GVector2D> side1Pts = sideRigde[0]->curve.getPoints(curvePointsCount);
    std::vector<GVector2D> side2Pts = sideRigde[1]->curve.getPoints(curvePointsCount);
    std::vector<GVector2D> topPts = topRidge ? topRidge->curve.getPoints(curvePointsCount) : std::vector<GVector2D>();

    std::vector<GVector2D> pts;
    pts.reserve(curvePointsCount * 4 - 3);
    std::vector<GVector2D> additionalPts;
    additionalPts.reserve(curvePointsCount + 1);

    additionalPts.insert(additionalPts.end(), bottomPts.begin(), bottomPts.end());
    additionalPts << bottomCurve.restrictionPolygon[3];

    bottomPts.pop_back();
    pts.insert(pts.end(), bottomPts.begin(), bottomPts.end());
    std::reverse(side2Pts.begin(), side2Pts.end());
    side2Pts.pop_back();
    pts.insert(pts.end(), side2Pts.begin(), side2Pts.end());
    if (!topPts.empty())
    {
        if (!isTopRidgeReversed)
            std::reverse(topPts.begin(), topPts.end());
        topPts.pop_back();
        pts.insert(pts.end(), topPts.begin(), topPts.end());
    }
    side1Pts.pop_back();
    pts.insert(pts.end(), side1Pts.begin(), side1Pts.end());

    return { Polygon2D(std::move(pts)), Polygon2D(std::move(additionalPts)) };
}

void DuneGraph::cachePoints(DuneFace& face, const std::function<float(const GVector2D&)>& bottomHeightFunc)
{
    constexpr int pointsCount = 10;

    face.points.reserve(pointsCount * 4);
    face.yPts.reserve(pointsCount * 4);

    const int nodeId1 = vertices[face.startVertex].address.nodeId;
    const int nodeId2 = vertices[face.endVertex].address.nodeId;
    const float maxHeight1 = getNode(nodeId1).height;
    const float maxHeight2 = getNode(nodeId2).height;

    const float h1Bottom = bottomHeightFunc(face.sideRigde[0]->curve.evaluate(1.f));
    const float h2Bottom = bottomHeightFunc(face.sideRigde[1]->curve.evaluate(1.f));

    const float mh1 = face.isTopRidgeReversed ? maxHeight2 : maxHeight1;
    const float mh2 = face.isTopRidgeReversed ? maxHeight1 : maxHeight2;

    // 1 ridge
    for (int i = 0; i < pointsCount; ++i)
    {
        const float t = i / (float)(pointsCount - 1.f);
        face.points << face.sideRigde[0]->curve.evaluate(t);
        face.yPts << std::lerp(maxHeight1, h1Bottom, face.sideRigde[0]->heightFunc(t));
    }

    // 2 ridge
    for (int i = 0; i < pointsCount; ++i)
    {
        const float t = i / (float)(pointsCount - 1.f);
        face.points << face.sideRigde[1]->curve.evaluate(t);
        face.yPts << std::lerp(maxHeight2, h2Bottom, face.sideRigde[1]->heightFunc(t));
    }

    // top ridge
    if (face.topRidge)
    {
        for (int i = 1; i < pointsCount - 1; ++i)
        {
            const float t = i / (float)(pointsCount - 1.f);
            face.points << face.topRidge->curve.evaluate(t);

            face.yPts << std::lerp(mh1, mh2, face.topRidge->heightFunc(t));
        }
    }

    face.ridgesPointsCount = face.points.size();

    // bottom ridge
    for (int i = 1; i < pointsCount; ++i)
    {
        const float t = i / (float)(pointsCount - 1.f);
        face.points << face.bottomCurve.curve.evaluate(t);
        face.yPts << bottomHeightFunc(face.points.back());
    }

    // intermediate inner slices - needed to form concave face surface
    for (float k = 0.3f; k < 1.f; k += 0.3f)
    {
        const GVector2D bottomCenter = face.bottomCurve.curve.evaluate(k);
        const float bottomCenterHeight = bottomHeightFunc(bottomCenter);
        const GVector2D topCenter = face.topRidge ? face.topRidge->curve.evaluate(k) : face.sideRigde[0]->curve.evaluate(0.f);
        const float topCenterHeight = face.topRidge ? std::lerp(mh1, mh2, face.topRidge->heightFunc(k)) : maxHeight1;

        constexpr float intermediatePointsCount = 10;
        for (int i = 1; i < intermediatePointsCount - 1; ++i)
        {
            const float t = i / (float)(intermediatePointsCount - 1.f);
            const auto interpolation = Interpolation::getInterpolation01(EInterpolation01::Power, 1.3f - 2.f *fabs(0.5f - k));
            const float tt = (float)interpolation->interpolate(t);
            face.points << std::lerp(bottomCenter, topCenter, t);
            face.yPts << std::lerp(bottomCenterHeight, topCenterHeight, tt);
        }
    }

    face.xPts.resize(face.points.size());
    face.zPts.resize(face.points.size());
    for (int i = 0; i < face.points.size(); ++i)
    {
        face.xPts[i] = face.points[i].x;
        face.zPts[i] = face.points[i].z;
    }
}


void DuneGraph::generateRidgesAndFaces(const std::vector<HeightFunction>& ridgeHeightFuncVec, const std::vector<HeightFunction>& topRidgeHeightFuncVec, const std::function<float(const GVector2D&)>& bottomHeightFunc)
{
    if (vertices.empty())
        return;

    sideRidges.reserve(vertices.size());
    topRidges.reserve(connections.size());
    faces.reserve(vertices.size() - 1);

    constexpr int numSegmentsParts = 10;
    constexpr float bezierWidthFactor = 0.9f;
    constexpr float sideRidgeWidthFactor = 0.6f;

    const DuneVertex* current = &vertices.front();
    const auto blankHeightFunc = [](float){ return 0.f; };
    std::uniform_int_distribution<> distribRidgeHF(0, ridgeHeightFuncVec.size() - 1);
    std::uniform_int_distribution<> distribTopRidgeHF(0, topRidgeHeightFuncVec.size() - 1);
    while (true)
    {
        const int currIdx = current->address.vertexId;
        const int nextIdx = current->next.vertexId;
        const DuneVertex& next = vertices[nextIdx];
        const int currNodeId = current->address.nodeId;
        const int nextNodeId = next.address.nodeId;

        DuneFace duneFace;
        duneFace.startVertex = currIdx;
        duneFace.endVertex = nextIdx;

        Polygon2D bottomRP = getBottomRidgeRestrictionPolygon(*current, next);
        duneFace.bottomCurve = { computeRandomBezierCurve(bottomRP, numSegmentsParts, bezierWidthFactor), blankHeightFunc, std::move(bottomRP) };

        Polygon2D sideRP = getSideRidgeRestrictionPolygon(*current);
        sideRidges[currIdx] = std::make_shared<DuneCurve>(
            computeRandomBezierCurve(sideRP, numSegmentsParts, sideRidgeWidthFactor),
            ridgeHeightFuncVec[distribRidgeHF(Generation::gRandomEngine)],
            std::move(sideRP));
        duneFace.sideRigde[0] = sideRidges[currIdx];

        if (!faces.empty())
            faces.back().sideRigde[1] = duneFace.sideRigde[0];

        if (currNodeId != nextNodeId) // face between nodes
        {
            const int connectionIdx = getConnectionIdx(currNodeId, nextNodeId);
            const auto& connection = getConnection(connectionIdx);
            const auto iter = topRidges.find(connectionIdx);
            if (iter == topRidges.end())
            {
                const Polygon2D topRP = getTopRidgeRestrictionPolygon(connection);
                topRidges[connectionIdx] = std::make_shared<DuneCurve>(
                    computeRandomBezierCurve(topRP, numSegmentsParts, bezierWidthFactor),
                    topRidgeHeightFuncVec[distribTopRidgeHF(Generation::gRandomEngine)],
                    std::move(topRP));
                duneFace.topRidge = topRidges[connectionIdx];
            }
            else
                duneFace.topRidge = iter->second;
            duneFace.isTopRidgeReversed = connection.from != currNodeId;
        }

        faces <<= std::move(duneFace);

        if (nextIdx == 0) // cycle completed, the initial vertex is found
        {
            faces.back().sideRigde[1] = faces.front().sideRigde[0];
            break;
        }
        current = &next;
    }

    // cache control points, needed for interpolation
    for (DuneFace& face: faces)
        cachePoints(face, bottomHeightFunc);

    bottomHeightFunction = bottomHeightFunc;
}


const std::vector<DuneFace>& DuneGraph::getFaces() const
{
    return faces;
}


float DuneGraph::getPointOnFaceHeight(const DuneFace& face, const GVector2D& point) const
{
    constexpr float smooth = 0.1f;
    gte::IntpThinPlateSpline2<float> interp(face.xPts.size(), &face.xPts[0], &face.zPts[0], &face.yPts[0], smooth, true);
    const float currentHeight = interp(point.x, point.z);
    return currentHeight;
}


Polygon2D DuneGraph::getBoundingPolygon() const
{
    std::vector<GVector2D> pts;
    pts.reserve(faces.size() * 2);

    for (const DuneFace& face: faces)
    {
        const Polygon2D edgePolygon = face.getPolygons(3).back();
        const auto& edgePolygonPts = edgePolygon.getPts();
        pts << edgePolygonPts.front();
        pts << edgePolygonPts.back();
    }

    return Polygon2D(std::move(pts));
}


void DuneGraph::debugDraw(int flags)
{
    if (flags & (int)DebugDrawFlags::ConnectionsScheme)
        GeomGraph<DuneNode, DuneConnection, DuneVertex>::debugDraw(flags);

    constexpr int curvePointsCount = 30;
    for (const DuneFace& face : faces)
    {
        if (flags & (int)DebugDrawFlags::BottomRPolygons)
            face.bottomCurve.restrictionPolygon.debugPlot(Colors::green, 201.f);

        if (flags & (int)DebugDrawFlags::Curves)
        {
            auto bezierCurvePts = face.bottomCurve.curve.getPoints(curvePointsCount);
            spawn<DLineMarker>(bezierCurvePts, Colors::springGreen, false, 200.f);

            bezierCurvePts = face.sideRigde[0]->curve.getPoints(curvePointsCount);
            spawn<DLineMarker>(bezierCurvePts, Colors::red, false, 200.f);

            bezierCurvePts = face.sideRigde[1]->curve.getPoints(curvePointsCount);
            spawn<DLineMarker>(bezierCurvePts, Colors::red, false, 200.f);
        }

        if (flags & (int)DebugDrawFlags::RidgeRPolygons)
        {
            face.sideRigde[0]->restrictionPolygon.debugPlot(Colors::laRioja, 202.f);
            face.sideRigde[1]->restrictionPolygon.debugPlot(Colors::laRioja, 202.f);
        }

        if (face.topRidge)
        {
            if (flags & (int)DebugDrawFlags::TopRidgeRPolygons)
                face.topRidge->restrictionPolygon.debugPlot(Colors::laRioja, 203.f);
            if (flags & (int)DebugDrawFlags::Curves)
            {
                const auto bezierCurvePts = face.topRidge->curve.getPoints(curvePointsCount);
                spawn<DLineMarker>(bezierCurvePts, Colors::orange, false, 200.f);
            }
        }
    }
}


MeshConnector DuneGraph::createMesh(const Polygon2D& clusterPolygon,
    const std::vector<Polygon2D>& surroundingPolygons,
    const std::function<QVector3D(const QVector3D&)>& surroundingPoint3dFunction,
    const std::function<float(const GVector2D& pt)>& bottomEdgeHeightFunction) const
{
    MeshConnector meshConnector;

    for (const auto& sP : surroundingPolygons)
    {
        const auto [geom2D, _] = meshPolygon2(sP.getPts());
        meshConnector.addMesh(geom2D.vertices, geom2D.indices, surroundingPoint3dFunction);
    }

    // Faces
    constexpr int curvePointsCount = 18;
    for (const DuneFace& face: getFaces())
    {
        const auto duneFace3dFunc = [&face, this](const QVector3D& v) -> QVector3D
        {
            const GVector2D pt = (GVector2D)v;
            const float duneHeight = this->getPointOnFaceHeight(face, pt);
            return QVector3D(v.x(), duneHeight, v.z());
        };

        const auto [facePolygon, bottomAdditionalPolygon] = face.getPolygons(curvePointsCount);

        const auto [geomAP, _] = meshPolygon2(bottomAdditionalPolygon.getPts());
        meshConnector.addMesh(geomAP.vertices, geomAP.indices, surroundingPoint3dFunction);

        static auto faceMeshingParams = []()
        {
            MeshingParams params = getDefaultMeshingParams();
            params.innerSplitFunc = [](const GVector2D& p1, const GVector2D& p2, FFirstLastPolicy policy) 
            { 
                return splitSegment(Segment2D{ p1, p2 }, policy, true, int(1.8f * getMeshSegmentsAdv(p1, p2)));
            };
            return params;
        }();
        

        const auto [geomFP, _2] = meshPolygon2(facePolygon.getPts(), faceMeshingParams);
        meshConnector.addMeshWithoutCheckingDublicates(geomFP.vertices, geomFP.indices, duneFace3dFunc);

#if DEBUG_RIDGES
        auto&& dem = Generation::Data::get()->getDEM();
        std::vector<QVector3D> ridgePts;
        std::vector<QVector3D> bottomPts;
        for (int i = 0; i < curvePointsCount; ++i)
        {
            // ridges
            const float t = i / float(curvePointsCount - 1.f);
            const GVector2D ptR = face.sideRigde[0]->curve.evaluate(t);
            ridgePts << duneFace3dFunc((QVector3D)ptR);
            // bottom
            const GVector2D pt = face.bottomCurve.curve.evaluate(t);
            bottomPts << duneFace3dFunc((QVector3D)pt);
        }

        spawn<DLineMarker>(ridgePts, Colors::orange, false);
        spawn<DLineMarker>(bottomPts, Colors::red, false);
#endif
    }

    return meshConnector;
}


MeshConnector DuneGraph::meshClusterPolygon(const Polygon2D& clusterPolygon, const std::function<float(const GVector2D& pt)>& bottomEdgeHeightFunction)
{
    // Height function ---------------------------------------------------------------------------------------------

    // cache face polygons
    constexpr int curvePointsCount = 18;
    std::vector<Polygon2D> facePolygons;
    facePolygons.reserve(getFaces().size());
    for (const DuneFace& face: getFaces())
        facePolygons << std::get<0>(face.getPolygons(curvePointsCount));

    const int approximationRidgePointsCount = (sideRidges.size() + topRidges.size()) * curvePointsCount + nodes.size();

    std::unordered_map<GVector2D, float> ridgePointsYMap;
    std::vector<std::vector<int>> sortedFacesPoints;

    ridgePointsYMap.reserve(approximationRidgePointsCount * 2);
    sortedFacesPoints.resize(faces.size());
    for (auto& vec: sortedFacesPoints)
        vec.reserve(100);

    // Mesh ----------------------------------------------------------------------------------------------------------
    MeshConnector meshConnector;

    // Additional points for ridge ------------------------------------------------------
    std::vector<GVector2D> ridgePoints;
    ridgePoints.reserve((sideRidges.size() + topRidges.size()) * curvePointsCount + nodes.size());

    const auto addRidgePointsFunc = [&clusterPolygon, &ridgePoints, &ridgePointsYMap](const std::unordered_map<int, std::shared_ptr<DuneCurve>>& ridges, const std::function<float(int, float)>& ridgeFuncY)
    {
        for (const auto& ridge: ridges)
        {
            const float curveLength = ridge.second->curve.getLength(6);
            const int currentCurvePtsCount = getMeshSegments(curveLength) * 1.8f;
            const auto points = ridge.second->curve.getPoints(currentCurvePtsCount);
            for (int i = 1; i < points.size() - 1; ++i)
            {
                const GVector2D& pt = points[i];
                if (clusterPolygon.contains(pt, false))
                {
                    ridgePoints << pt;
                    const GVector2D offsetDuplicate = pt + GVector2D(0.2f, 0.2f);
                    if (clusterPolygon.contains(offsetDuplicate, false))
                        ridgePoints << offsetDuplicate;

                    const float t = i / float(points.size() - 1.f);
                    const float y = ridgeFuncY(ridge.first, t);
                    ridgePointsYMap[pt] = y;
                    ridgePointsYMap[offsetDuplicate] = y;
                }
            }
        }
    };

    addRidgePointsFunc(sideRidges, [this](int index, float curveParam){ return this->getSideRidgePointHeight(index, curveParam); });
    addRidgePointsFunc(topRidges,  [this](int index, float curveParam){ return this->getTopRidgePointHeight( index, curveParam); });

    for (const auto& node: nodes)
    {
        if (clusterPolygon.contains(node.center, false))
        {
            ridgePoints << node.center;
            ridgePointsYMap[node.center] = node.height;
        }
    }

    // add points on faces for better mesh topology
    for (const auto& face: getFaces())
    {
        const GVector2D bp1 = face.bottomCurve.curve.evaluate(0.f);
        const GVector2D bp2 = face.bottomCurve.curve.evaluate(1.f);
        const GVector2D tp1 = face.sideRigde[0]->curve.evaluate(0.f);
        const GVector2D tp2 = face.sideRigde[1]->curve.evaluate(0.f);
        const GVector2D bp = (bp1 + bp2) * 0.5f;
        const GVector2D tp = (tp1 + tp2) * 0.5f;

        const std::array<GVector2D, 4> cpts = {
            std::lerp(bp, tp, 0.2f),
            std::lerp(bp, tp, 0.4f),
            std::lerp(bp, tp, 0.6f),
            std::lerp(bp, tp, 0.8f)
        };

        for (const auto& pt: cpts)
        {
            if (clusterPolygon.contains(pt, false))
            {
                ridgePoints << pt;
                // spawn<DLineMarker>((QVector3D)pt, 1000.f);
            }
        }
    }

    // ----------------------------------------------------------------------------------

    const auto [geom2D, _] = meshPolygon2(clusterPolygon.getPts(), getDefaultMeshingParams());
    auto& vertsP = geom2D.vertices;
    auto& indP = geom2D.indices;

    std::unordered_map<GVector2D, float> pointToYMap; // point from vertsP -> y height;
    pointToYMap.reserve(vertsP.size());

    for (int i = 0; i < vertsP.size(); ++i)
    {
        const GVector2D& pt = vertsP[i];

        const auto iter = ridgePointsYMap.find(pt);
        if (iter != ridgePointsYMap.end())
        {
            pointToYMap[vertsP[i]] = iter->second;
            continue;
        }

        const auto& faces = this->getFaces();
        bool isPointOnDune = false;
        for (int j = 0; j < faces.size(); ++j)
        {
            if (facePolygons[j].contains(pt))
            {
                sortedFacesPoints[j] << i;
                isPointOnDune = true;
                break;
            }
        }

        if (isPointOnDune)
            continue;

        pointToYMap[vertsP[i]] = bottomEdgeHeightFunction(pt);
    }

    for (int i = 0; i < sortedFacesPoints.size(); ++i)
    {
        const auto& currentFacePoints = sortedFacesPoints[i];
        for (int j = 0; j < currentFacePoints.size(); ++j)
        {
            const GVector2D& pt = vertsP[currentFacePoints[j]];
            const float resHeight = getPointOnFaceHeight(faces[i], pt);
            pointToYMap[pt] = resHeight;
        }
    }

    const auto duneFace3dFunc = [this, &pointToYMap](const QVector3D& v) -> QVector3D
    {
        const GVector2D pt = (GVector2D)v;
        const float duneHeight = pointToYMap[pt];
        return QVector3D(v.x(), duneHeight, v.z());
    };

// #if DEBUG_RIDGES
//     const auto debugDrawRidgesFunc = [&duneFace3dFunc](const std::unordered_map<int, std::shared_ptr<DuneCurve>>& ridges, const QVector4D& color)
//     {
//         for (auto&& duneCurve: ridges)
//         {
//             std::vector<QVector3D> ridgePts;
//             ridgePts.reserve(curvePointsCount);
//             for (int i = 0; i < curvePointsCount; ++i)
//             {
//                 const float t = i / float(curvePointsCount - 1.f);
//                 const GVector2D ptR = duneCurve.second->curve.evaluate(t);
//                 ridgePts << duneFace3dFunc((QVector3D)ptR);
//             }
//             spawn<DLineMarker>(ridgePts, color, false);
//         }
//     };

//     debugDrawRidgesFunc(sideRidges, Colors::orange);
//     debugDrawRidgesFunc(topRidges, Colors::red);
// #endif

    meshConnector.addMesh(vertsP, indP, duneFace3dFunc);

    return meshConnector;
}


Polygon2D DuneGraph::getSideRidgeRestrictionPolygon(const DuneVertex& vertex) const
{
    const DuneNode& node = nodes[vertex.address.nodeId];
    const DuneVertex& prev = vertices[vertex.prev.vertexId];
    const DuneVertex& next = vertices[vertex.next.vertexId];

    const GVector2D bottomPtPrev = (vertex.point + prev.point) * 0.5f;
    const GVector2D bottomPtNext = (vertex.point + next.point) * 0.5f;
    const GVector2D topPtPrev = (prev.address.nodeId == vertex.address.nodeId) ? node.center : (node.center + getNode(prev.address.nodeId).center) * 0.5f;
    const GVector2D topPtNext = (next.address.nodeId == vertex.address.nodeId) ? node.center : (node.center + getNode(next.address.nodeId).center) * 0.5f;

    const GVector2D prevPt = std::lerp(topPtPrev, bottomPtPrev, 0.5f);
    const GVector2D nextPt = std::lerp(topPtNext, bottomPtNext, 0.5f);

    return Polygon2D(std::vector{ node.center, prevPt, vertex.point, nextPt });
}


Polygon2D DuneGraph::getTopRidgeRestrictionPolygon(const DuneConnection& connection) const
{
    const DuneNode& node = getNode(connection.from);
    const DuneNode& nextNode = getNode(connection.to);
    GVector2D fromV1, fromV2, toV1, toV2;
    for (const int vId : node.verticesIdx)
    {
        const DuneVertex& curr = getVertex(vId);
        const DuneVertex& prev = getVertex(curr.prev.vertexId);
        if (prev.address.nodeId == connection.to)
        {
            fromV1 = curr.point;
            toV1 = prev.point;
            break;
        }
    }

    for (const int vId : node.verticesIdx)
    {
        const DuneVertex& curr = getVertex(vId);
        const DuneVertex& next = getVertex(curr.next.vertexId);
        if (next.address.nodeId == connection.to)
        {
            fromV2 = curr.point;
            toV2 = next.point;
            break;
        }
    }

    const GVector2D nodesAvPt = (node.center + nextNode.center) * 0.5f;
    const GVector2D avPt1 = (fromV1 + toV1) * 0.5f;
    const GVector2D avPt2 = (fromV2 + toV2) * 0.5f;
    const GVector2D rpt1 = std::lerp(nodesAvPt, avPt1, bottomCurveTopPointFactor);
    const GVector2D rpt2 = std::lerp(nodesAvPt, avPt2, bottomCurveTopPointFactor);

    return Polygon2D(std::vector{ node.center, rpt1, nextNode.center, rpt2 });
}


Polygon2D DuneGraph::getBottomRidgeRestrictionPolygon(const DuneVertex& vertex, const DuneVertex& nextVertex) const
{
    const DuneNode& node = getNode(vertex.address.nodeId);
    const DuneNode& nextNode = getNode(nextVertex.address.nodeId);
    const GVector2D bottomPt = (vertex.point + nextVertex.point) * 0.5f;
    const GVector2D topNodePt = (nextVertex.address.nodeId == vertex.address.nodeId) ? node.center : (node.center + nextNode.center) * 0.5f;

    const GVector2D topPt = std::lerp(topNodePt, bottomPt, bottomCurveTopPointFactor);
    return Polygon2D(std::vector{ vertex.point, topPt, nextVertex.point, bottomPt });
}


float DuneGraph::getSideRidgePointHeight(int vertexId, float curveParam) const
{
    const auto ridge = sideRidges.find(vertexId)->second;

    const int nodeId = vertices[vertexId].address.nodeId;
    const float maxHeight = getNode(nodeId).height;
    const float minHeight = bottomHeightFunction(ridge->curve.evaluate(1.f));

    return std::lerp(maxHeight, minHeight, ridge->heightFunc(curveParam));
}


float DuneGraph::getTopRidgePointHeight(int connectionId, float curveParam) const
{
    const auto ridge = topRidges.find(connectionId)->second;
    const auto& connection = getConnection(connectionId);

    const int nodeId1 = connection.from;
    const int nodeId2 = connection.to;
    const float height1 = getNode(nodeId1).height;
    const float height2 = getNode(nodeId2).height;

    return std::lerp(height1, height2, ridge->heightFunc(curveParam));
}
