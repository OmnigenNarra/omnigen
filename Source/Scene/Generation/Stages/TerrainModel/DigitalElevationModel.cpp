#include "stdafx.h"

#include "DigitalElevationModel.h"

#include <Omnigen.h>
#include <Scene/Generation/OmnigenGeneration.h>
#include <Scene/Generation/Common/Markers/LineMarker.h>
#include <Scene/Generation/Stages/Landmasses/LandmassBoundMarker.h>
#include "Utils/Polygon.h"
#include <Editor/Sections/Profiler/OmnigenProfiler.h>
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/Stages/ContourLines/IsohypseBatchingMarker.h"

#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <tbb/task_group.h>

#include <noise/noise.h>

namespace Generation
{
    DEM::DEM(const GVector2D& minXZ, const GVector2D& maxXZ, float inSpacing)
        : heightData(minXZ, maxXZ, inSpacing)
        , levelData(minXZ, maxXZ, inSpacing)
        , verticalDisplacementXCoords(minXZ, maxXZ, inSpacing)
    {
        auto&& updateInfo = QSharedPointer<DEMUpdateInfo>::create();
        updateInfo->points.resize(heightData.getGeometryRW()->vertices.size());
        for (IndexType i = 0; i < updateInfo->points.size(); i++)
            updateInfo->points[i] = heightData.getPoint2D(i);

        emit Editable::created(updateInfo);
    }

    void DEM::addHeightToPoints(const std::unordered_map<IndexType, float>& pointsToUpdate)
    {
        auto&& indexes = std::views::keys(pointsToUpdate);
        auto&& updateInfo = QSharedPointer<DEMUpdateInfo>::create();
        std::transform(pointsToUpdate.begin(), pointsToUpdate.end(), std::back_inserter(updateInfo->points), [&](auto& kv) { return heightData.getPoint2D(kv.first); });

        emit Editable::aboutToBeModified(updateInfo);
        tbb::parallel_for(0, int(indexes.size()), [&](int i)
            {
                auto&& idx = *std::next(indexes.begin(), i);
                heightData.addHeight(idx, pointsToUpdate.at(idx));
            });
        emit Editable::modified(updateInfo);
    }

    void DEM::updatePointsNormal(const std::unordered_map<IndexType, QVector3D>& pointsToUpdate)
    {
        auto&& indexes = std::views::keys(pointsToUpdate);
        auto&& updateInfo = QSharedPointer<DEMUpdateInfo>::create();
        std::transform(pointsToUpdate.begin(), pointsToUpdate.end(), std::back_inserter(updateInfo->points), [&](auto& kv) { return heightData.getPoint2D(kv.first); });

        emit Editable::aboutToBeModified(updateInfo);
        tbb::parallel_for(0, int(indexes.size()), [&](int i)
            {
                auto&& idx = *std::next(indexes.begin(), i);
                heightData.setNormal(idx, pointsToUpdate.at(idx));
            });
        emit Editable::modified(updateInfo);
    }

    void DEM::reshapeGrid()
    {
        // Calculate DEM dimensions
        GPoint minSquare = { GRID_SEGMENT_COUNT, GRID_SEGMENT_COUNT };
        GPoint maxSquare = { 0, 0 };

        for (auto&& [hnd, domain] : Generation::Data::get()->getAllDomains())
            if (domain->getType() == EDomainType::Terrain || domain->getType() == EDomainType::Water)
                for (auto&& sq : domain->getSquares())
                {
                    minSquare.x = std::min(minSquare.x, sq.x);
                    maxSquare.x = std::max(maxSquare.x, sq.x);
                    minSquare.z = std::min(minSquare.z, sq.z);
                    maxSquare.z = std::max(maxSquare.z, sq.z);
                }

        GVector2D botCorner = GVector2D{ float(minSquare.x), float(minSquare.z) } * GRID_SEGMENT_WIDTH;
        GVector2D topCorner = GVector2D{ float(maxSquare.x + 1), float(maxSquare.z + 1) } * GRID_SEGMENT_WIDTH;
        GVector2D previousBotCorner = heightData.getBotCorner();
        GVector2D previousTopCorner = heightData.getTopCorner();

        if (botCorner == previousBotCorner && topCorner == previousTopCorner)
            return;

        auto&& updateAddedInfo = QSharedPointer<DEMUpdateInfo>::create();
        auto&& updateDeletedInfo = QSharedPointer<DEMUpdateInfo>::create();

        for (IndexType i = 0; i < heightData.getGeometryRW()->vertices.size(); i++)
            if (auto&& pt = heightData.getPoint2D(i); pt.x() < botCorner.x || pt.z() < botCorner.z || pt.x() > topCorner.x || pt.z() > topCorner.z)
                updateDeletedInfo->points << pt;
            else if (pt.x() < previousBotCorner.x || pt.z() < previousBotCorner.z || pt.x() > previousTopCorner.x || pt.z() > previousTopCorner.z)
                updateAddedInfo->points << pt;

        emit Editable::aboutToBeDeleted(updateDeletedInfo);

        heightData.reshapeGrid(botCorner, topCorner, gridSpacing);
        levelData.reshapeGrid(botCorner, topCorner, gridSpacing);
        verticalDisplacementXCoords.reshapeGrid(botCorner, topCorner, gridSpacing);

        emit Editable::created(updateAddedInfo);

        Generation::Data::get()->clearExactMarkers<DDemMarker>();
        heightData.makePreview<DDemMarker>(color);
    }

    CellElevationData DEM::getCellElevationData(const Polygon2D& cell) const
    {
        CellElevationData result;
        result.height = heightData.sample(cell.getCenter());

        GVector2D lowest, highest;

        for (auto&& p : cell)
        {
            float h = heightData.sample(p);

            if (h < result.minH)
            {
                lowest = p;
                result.minH = h;
            }

            if (h > result.maxH)
            {
                highest = p;
                result.maxH = h;
            }
        }

        result.steepness = (result.maxH - result.minH) / cell.getRadius();
        result.gradient = (highest - lowest).normalized();

        return result;
    }

    void DEM::initialize()
    {
        // Calculate DEM dimensions
        GPoint minSquare = { GRID_SEGMENT_COUNT, GRID_SEGMENT_COUNT };
        GPoint maxSquare = { 0, 0 };

        for (auto&& [hnd, domain] : Generation::Data::get()->getAllDomains())
            if (domain->getType() == EDomainType::Terrain || domain->getType() == EDomainType::Water)
                for (auto&& sq : domain->getSquares())
                {
                    minSquare.x = std::min(minSquare.x, sq.x);
                    maxSquare.x = std::max(maxSquare.x, sq.x);
                    minSquare.z = std::min(minSquare.z, sq.z);
                    maxSquare.z = std::max(maxSquare.z, sq.z);
                }

        // Init DEM
        GVector2D botCorner = GVector2D{ float(minSquare.x), float(minSquare.z) } *GRID_SEGMENT_WIDTH;
        GVector2D topCorner = GVector2D{ float(maxSquare.x + 1), float(maxSquare.z + 1) } *GRID_SEGMENT_WIDTH;
        Data::get()->setDEM(QSharedPointer<DEM>::create(botCorner, topCorner, gridSpacing));
    }

    void DEM::clear()
    {
        auto&& dem = Generation::Data::get()->getDEM();
        if (!dem)
            return;

        auto&& updateInfo = QSharedPointer<DEMUpdateInfo>::create();
        updateInfo->points.resize(dem->heightData.getGeometryRW()->vertices.size());
        for (IndexType i = 0; i < updateInfo->points.size(); i++)
            updateInfo->points[i] = dem->heightData.getPoint2D(i);

        emit Editable::aboutToBeDeleted(updateInfo);

        Generation::Data::get()->setDEM(nullptr);
    }

	noise::model::Plane& getTexcoordNoise()
	{
		static noise::module::Perlin noiseSource;
		static noise::model::Plane noiseModel;
		static bool isInit = false;

		static std::mutex guard;
		std::scoped_lock lock(guard);
		if (!isInit)
		{
			noiseSource.SetSeed(std::rand());
            noiseSource.SetFrequency(1.0 / 100'00.0);
            noiseSource.SetOctaveCount(8);
            noiseSource.SetLacunarity(1.5);
			noiseModel.SetModule(noiseSource);
			isInit = true;
		}

		return noiseModel;
	}

    void DEM::loadFromIHs(const std::vector<GVector2D>& pointsToLoad)
    {
        // Compute heights
        auto&& markers = Generation::Data::get()->getIsohypseMarkersByLevel();

        auto getExactIHDistance = [](const GVector2D& p, const IHSrcInfo& closestIHS)
        {
            std::vector<GVector2D> bound = { closestIHS.getPoint(-1), closestIHS.getPoint(0), closestIHS.getPoint(1) };
            return directionalBoundDistance(bound, p);
        };

        auto&& updateInfo = QSharedPointer<DEMUpdateInfo>::create();

        if (pointsToLoad.empty())
        {
            updateInfo->points.resize(heightData.getGeometryRW()->vertices.size());
            for (IndexType i = 0; i < updateInfo->points.size(); i++)
                updateInfo->points[i] = heightData.getPoint2D(i);
        }
        else
            updateInfo->points = pointsToLoad;

        emit Editable::aboutToBeModified(updateInfo);

        {
            OmniProfile("Height computing");
            auto&& ihQtree = gBatchingMarkerInstance<IsohypseBatchParams>->getQuadTree();

            auto assignPointHeight = [&](int x, int z, const GVector2D& point, const IHSrcInfo& lowerHInfo, const IHSrcInfo& higherHInfo)
            {
                float lowerHeight = lowerHInfo.ih->getHeight();
                float higherHeight = higherHInfo.ih->getHeight();

                //calculating control points for Bezier curve
                auto [projectPointToLowerMarker, distToLower, unusedIdx1] = getExactIHDistance(point, lowerHInfo);
                auto [projectPointToHigherMarker, distToHigher, unusedIdx2] = getExactIHDistance(point, higherHInfo);

                std::vector<QVector3D> controlPoints;
                int idxSegmentContainsPoint = 0;

                if (const auto& highestHInfo = higherHInfo.getSource(); highestHInfo.idx != -1)
                {
                    auto [projPt, dist, unusedIdx] = getExactIHDistance(projectPointToHigherMarker, highestHInfo);
                    controlPoints.emplace_back(projPt.x, highestHInfo.ih->getHeight(), projPt.z);
                    idxSegmentContainsPoint = 1;
                }
                controlPoints.emplace_back(projectPointToHigherMarker.x, higherHeight, projectPointToHigherMarker.z);
                controlPoints.emplace_back(projectPointToLowerMarker.x, lowerHeight, projectPointToLowerMarker.z);
                if (const auto& lowestHInfo = lowerHInfo.ih->getNearestDescendant(lowerHInfo.idx); lowestHInfo.idx != -1)
                {
                    auto [projPt, dist, unusedIdx] = getExactIHDistance(projectPointToLowerMarker, lowestHInfo);
                    controlPoints.emplace_back(projPt.x, lowestHInfo.ih->getHeight(), projPt.z);
                }

                if (controlPoints.size() == 2)
                {
                    float distSum = distToLower + distToHigher;
                    float lowerWeight = distToLower / distSum;
                    heightData.setHeight(x, z, std::lerp(lowerHeight, higherHeight, lowerWeight));
                    levelData.setHeight(x, z, float(lowerHInfo.ih->getLevel()) - lowerWeight);
                    return;
                }

                //calculate x coordinates of control point in Bezier curve dimension
                std::vector<float> xBezier;
                xBezier.push_back(0);
                for (size_t i = 1; i < controlPoints.size(); ++i)
                {
                    float dist = distance(GVector2D(controlPoints[i - 1]), GVector2D(controlPoints[i]));
                    xBezier.push_back(xBezier[i - 1] + dist);
                }

                //create 2D Bezier curve
                std::vector<GVector2D> pts;
                for (size_t i = 0; i < controlPoints.size(); ++i)
                {
                    pts.emplace_back(xBezier[i], controlPoints[i].y());
                }
                BezierCurve2D bezierCurve(pts);

                //find x coordinate of desired point in Bezier curve dimension 
                auto projPt = std::get<GVector2D>(distance({ projectPointToLowerMarker, projectPointToHigherMarker }, point));
                float xPoint =
                    xBezier[idxSegmentContainsPoint] + projectPointToHigherMarker.dist(projPt);

                //find appropriate t
                float low = 0;
                float high = 1.;
                auto pt = bezierCurve.evaluate(low);
                float EPS = 0.01;
                while (abs(pt.x - xPoint) > EPS)
                {
                    float mid = low + (high - low) / 2;
                    pt = bezierCurve.evaluate(mid);
                    if (pt.x < xPoint)
                    {
                        low = mid;
                    }
                    else
                    {
                        high = mid;
                    }
                }
                float height = pt.z;

                //Final height
                float heightDiff = higherHeight - lowerHeight;
                float lowerWeight = std::clamp(height - lowerHeight / heightDiff, 0.0f, 1.0f);
                heightData.setHeight(x, z, height);
                levelData.setHeight(x, z, float(lowerHInfo.ih->getLevel()) - lowerWeight);

                // Vertical coords
                double noiseValue = std::clamp(getTexcoordNoise().GetValue(point.x, point.z), -1.0, 1.0);
                verticalDisplacementXCoords.setHeight(x, z, (noiseValue + 1.0) / 2.0);
            };

            auto findClosestIHPoint = [&](const GVector2D& p)
            {
                float r = 200.f;
                while (true)
                {
                    auto nodes = ihQtree.map_all_nearest(p.x, p.z, r);
                    if (!nodes.empty())
                        return nodes.begin()->second->data;

                    r *= 2.f;
                }
            };

            auto findPointHeight = [&](int x, int z)
            {
                auto&& point = heightData.getPoint2D(x, z);

                auto closestIHPoint = findClosestIHPoint(point);
                auto&& pts = closestIHPoint.section->getCircularPoints();

                auto [isInside, closestPoint, minDistToLower] = point.isInsidePolygon(pts);
                IHSrcInfo lowerHInfo, higherHInfo;

                if (isInside)
                {
                    lowerHInfo = IHSrcInfo(closestIHPoint.section, closestIHPoint.idx);
                    higherHInfo = lowerHInfo.getSource();
                }
                else
                {
                    higherHInfo = IHSrcInfo(closestIHPoint.section, closestIHPoint.idx);
                    lowerHInfo = higherHInfo.ih->getNearestDescendant(higherHInfo.idx);
                }

                if (lowerHInfo && higherHInfo)
                    assignPointHeight(x, z, point, lowerHInfo, higherHInfo);
//                 else if (!lowerHInfo && higherHInfo)
//                 {
//                     if (isInside)
//                         spawn<DLineMarker>(point, 10000, Colors::purple);
//                     else
//                         spawn<DLineMarker>(point, 10000, Colors::red);
//                 }
//                 else if (!higherHInfo && lowerHInfo)
//                     spawn<DLineMarker>(point, 10000, Colors::blue);
//                 else
//                     spawn<DLineMarker>(point);
            };


            if (pointsToLoad.empty())
                tbb::parallel_for(tbb::blocked_range2d<int, int>(0, heightData.getSize().x, 0, heightData.getSize().z), [&](tbb::blocked_range2d<int>& r)
                    {
                        for (int z = r.cols().begin(); z <= r.cols().end(); ++z)
                            for (int x = r.rows().begin(); x <= r.rows().end(); ++x)
                                findPointHeight(x, z);
                    });
            else
                tbb::parallel_for(0, int(pointsToLoad.size()), [&](int i)
                    {
                        auto point = heightData.getCoords(pointsToLoad[i]);
                        findPointHeight(point.x, point.z);
                    });
        }

        heightData.updateNormals();
        heightData.update();
        verticalDisplacementXCoords.update();

        emit Editable::modified(updateInfo);
    }

}

void omniSave(const Generation::DEM& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.heightData;
	omniBin << object.levelData;
	omniBin << object.verticalDisplacementXCoords;
}

void omniLoad(Generation::DEM& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.heightData;
	omniBin >> object.levelData;
	omniBin >> object.verticalDisplacementXCoords;
}


void DDemMarker::bindShader(const OmnigenCamera& camera)
{
	shaderPipeline.bind();
	shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());

	auto* gl = QOpenGLContext::currentContext()->extraFunctions();
	shaderPipeline.setUniformValue(EShaderUniform::Texture3, 0);
	gl->glActiveTexture(GL_TEXTURE0);
	gl->glBindTexture(GL_TEXTURE_1D, gradientTexture);
}

void DDemMarker::draw()
{
	auto& vbo = geometry->vbo;
    vbo.bind();
    using namespace Generation;
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, offsetof(Heightfield::Vertex, height), 1, sizeof(Heightfield::Vertex));
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Normal, GL_FLOAT, offsetof(Heightfield::Vertex, normal), 3, sizeof(Heightfield::Vertex));
    vbo.release();

	ShaderPipeline::current->setUniformValue(EShaderUniform::Texture0, config.gridSpacing);
	ShaderPipeline::current->setUniformValue(EShaderUniform::Texture1, QVector2D(config.offsetX, config.offsetZ));
	ShaderPipeline::current->setUniformValue(EShaderUniform::Texture2, QVector2D(config.sizeX, config.sizeZ));

	auto&& quads = geometry->indices;
	glDrawElements(GL_QUADS, quads.size(), GL_UNSIGNED_INT, quads.data());
}


void DDemMarker::unbindShader()
{
	shaderPipeline.release();
}

void DDemMarker::createShader()
{
	if (shaderPipeline.isLinked())
		return;

	// Compute final vertex position.
	const char* vertexShaderSource =
		"#version " OPENGL_SHADER_VER "\n"
		"uniform mat4 viewProjection;\n\n"
		"uniform float gridSpacing;\n"
		"uniform vec2 offset;\n"
		"uniform vec2 size;\n"

		"in float height;\n"
        "in vec3 normal;\n"
		"out float outHeight;\n\n"
        "out vec3 pNormal;\n\n"
#if DEBUG_HEIGHTFIELD_TEXCOORD
		"out float coord;\n\n"
#endif

		"void main(void)\n"
		"{\n"
		"   int z = gl_VertexID / (int(size[0]) + 1);\n"
		"   int x = int(mod(double(gl_VertexID), double(size[0]) + 1.0));\n"
#if !DEBUG_HEIGHTFIELD_TEXCOORD
		"   gl_Position = viewProjection * vec4(float(offset[0] + x) * gridSpacing, height + normal.x, (offset[1] + z) * gridSpacing, 1);\n"
		"   outHeight = height;\n"
        "   pNormal = normal;\n"
#else
		"   gl_Position = viewProjection * vec4(float(offset[0] + x) * gridSpacing, 0, (offset[1] + z) * gridSpacing, 1);\n"
		"   coord = abs(0.5f - height) * 2.0f;\n"
#endif
		"}\n";

	// The final color depends on the height. Simple, but brings a lot.
    const char* fragmentShaderSource =
        "#version " OPENGL_SHADER_VER "\n"

        "uniform sampler1D gradient;\n"
        "uniform int objectID;\n\n"

        "in float outHeight;\n"
        "in vec3 pNormal;\n"

        "const vec3 lightDirection = vec3(0.57735026919f, -0.57735026919f, 0.57735026919f);\n"
        "const float ambient = 0.2f;\n\n"

        "layout (location = 0) out vec4 fragColor;\n"
        "layout (location = 1) out vec4 outData;\n\n"

#if DEBUG_HEIGHTFIELD_TEXCOORD
		"in float coord;\n\n"
#endif

		"void main(void)\n"
		"{\n"
		"   const float maxH = 20000.0f + pNormal.x;\n\n"
        "   float f = clamp(outHeight / maxH, 0.0f, 1.0f);\n"
        "   fragColor = texture(gradient, f);\n"
        "   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"

#if DEBUG_HEIGHTFIELD_TEXCOORD
		"   fragColor = color * coord;\n"
		"   fragColor.a = 1;\n"
#else
        "   float factor = dot(lightDirection, -pNormal);\n"
        "   factor = clamp(factor, 0.0f, 1.0f - ambient) + ambient;\n"
        "   fragColor = fragColor * factor;\n"
        "   fragColor.w = 1;\n"
#endif
		"};\n";

	shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
	shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
	bool ok = shaderPipeline.link();

	shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
	shaderPipeline.set(EShaderUniform::Texture0, "gridSpacing");
	shaderPipeline.set(EShaderUniform::Texture1, "offset");
	shaderPipeline.set(EShaderUniform::Texture2, "size");
	shaderPipeline.set(EShaderUniform::Texture3, "gradient");

	shaderPipeline.set(EShaderAttribute::Position, "height");
    shaderPipeline.set(EShaderAttribute::Normal, "normal");

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}

void DDemMarker::createShaderResources()
{
    if (!gradient.empty())
        return;

    // TODO: UI for this data
    static const std::vector<std::pair<float, QVector4D>> gradientData =
    {
        { 0.0f, QVector4D(0.0, 0.2, 0.0, 1.0) },
        { 0.2f, QVector4D(0.0, 0.4, 0.0, 1.0) },
        { 0.5f, QVector4D(0.7, 0.7, 0.0, 1.0) },
        { 0.8f, QVector4D(0.6, 0.0, 0.0, 1.0) },
        { 1.0f, QVector4D(0.3, 0.0, 0.0, 1.0) },
    };

    auto computeGradientValue = [&](float f) -> QVector4D
    {
        for (IndexType i = 0; i < gradientData.size(); ++i)
            if (gradientData[i].first > f)
                return std::lerp(gradientData[i - 1].second, gradientData[i].second, (f - gradientData[i - 1].first) / (gradientData[i].first - gradientData[i - 1].first));

        Q_ASSERT(false);
        return {};
    };

    const size_t detail = 512;
    gradient.resize(detail);
    gradient.front() = gradientData.begin()->second;
    gradient.back() = gradientData.rbegin()->second;
    for (size_t i = 1; i < detail - 1; ++i)
        gradient[i] = computeGradientValue(float(i) / float(detail - 1));

    glGenTextures(1, &gradientTexture);
    glBindTexture(GL_TEXTURE_1D, gradientTexture);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, detail, 0, GL_RGBA, GL_FLOAT, gradient.data());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_1D, 0);
}
