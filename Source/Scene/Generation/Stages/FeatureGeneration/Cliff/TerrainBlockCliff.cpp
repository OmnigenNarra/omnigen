#include "stdafx.h"

#include "TerrainBlockCliff.h"
#include "Omnigen.h"
#include "Scene/Generation/Common/Markers/LineMarker.h"
#include "Scene/Generation/Stages/Landmasses/StageGeneration_Landmasses.h"
#include "Utils/Interpolation.h"
#include <gli/sampler2d.hpp>

#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Data/Assets/RockMaterial/AssetRockMaterial.h"
#include <Mathematics/GenerateMeshUV.h>
#include "Scene/Generation/Stages/ContourLines/ContourLines.h"
#include "Scene/Generation/Stages/Lithomap/StageGeneration_Lithomap.h"
#include "Scene/Generation/OmnigenGeneration.h"
#include "../Fault/TerrainBlockFault.h"
#include <noise/noise.h>
#include "Scene/Generation/Common/Markers/PointCloudMarker.h"
#include "Utils/Resumable.h"

namespace Generation
{
    using CliffCluster = TerrainBlockCluster<ETerrainBlock::Cliff>;

    float CliffCluster::chance(const BlockChanceData& data)
    {
        if (data.isWithinShoreDist && data.maxH >= 100)
            return 1000.0f;

        return 0.0f;
    }

    static float getDemHeight(const GVector2D& pt)
    {
        return Data::get()->getDEM()->heightData.sample(pt);
    }

    std::vector<std::vector<GVector2D>> CliffCluster::makeLines() const
    {
		auto&& heightData = Data::get()->getDEM()->heightData;

        std::map<float, int> borderPointsByHeight;
        for (int i = 0; i < clusterBorderPoints.size(); ++i)
			borderPointsByHeight[heightData.sample(clusterBorderPoints[i])] = i;

		// Remove top and bottom points, we want to connect pairs of points of similar height
		borderPointsByHeight.erase(borderPointsByHeight.begin()->first);
		borderPointsByHeight.erase(borderPointsByHeight.rbegin()->first);

		std::vector<std::vector<GVector2D>> lines;

		auto pickDirection = [&](const GVector2D& p)
		{
			auto gradient = heightData.sampleGradient(p);
			if (gradient.isNull())
			{
				spawn<DLineMarker>(p, 10000, Colors::red);
				return 0;
			}

			auto left = gradient.rotatedLeft90().normalized();

			if (clusterPolygon.contains(p + left * 5.0f))
				return 1; // go left
			else if (clusterPolygon.contains(p - left * 5.0f))
				return -1; // go right
			else
			{
				spawn<DLineMarker>(p, 10000, Colors::yellow);
				return 0; // corner case, skip
			}
		};

		while (true)
		{
			if (borderPointsByHeight.size() < 2)
				break;

			// Pop 1 point
			auto [h, pIdx] = *borderPointsByHeight.begin();
			borderPointsByHeight.erase(h);

			int dir = pickDirection(clusterBorderPoints[pIdx]);
			if (dir == 0)
				continue;

			float fDir = dir;
			std::vector<GVector2D> line = { clusterBorderPoints[pIdx] };
			bool lineValid = false;

			const float maxStep = 100.0f;

			// TODO Enable when done
			//std::uniform_real_distribution<float> stepDist(0.1f * maxStep, 1.0f * maxStep);
			auto stepDist = [](auto&& r) { return 50.0f; };

			while (true)
			{
				const auto& prevP = line.back();
				GVector2D nextP = line.back();

				auto g = heightData.sampleGradient(prevP).rotatedLeft90();
				if (g.isNull())
				{
					Q_ASSERT(line.size() > 1);
					nextP += (prevP - *++line.rbegin()).normalized() * stepDist(gRandomEngine);
				}
				else
				{
					nextP += g.normalized() * fDir * stepDist(gRandomEngine);
				}

				// Inside
				if (clusterPolygon.contains(nextP))
				{
					line << nextP;
					continue;
				}

				// Going out
				auto intersections = clusterPolygon.rayIntersections({ prevP, nextP });
				Q_ASSERT(!intersections.empty());

				auto&& [edgeP, unused] = intersections.begin().value();
				line << edgeP;
				int closestBpIdx = -1;

				// Find closest point in bps
				float minD = std::numeric_limits<float>::max();
				for (int i = 0; i < clusterBorderPoints.size(); ++i)
					if (float d = distance(clusterBorderPoints[i], edgeP); d < minD)
					{
						minD = d;
						closestBpIdx = i;
					}

				for (auto&& [h, bpIdx] : borderPointsByHeight)
					if (bpIdx == closestBpIdx)
					{
						line.back() = clusterBorderPoints[bpIdx];
						borderPointsByHeight.erase(h);
						lineValid = true;
						break;
					}

				break;
			}

			if (lineValid)
			{
				if (dir < 0)
					std::ranges::reverse(line);

				lines << line;
			}
		}

// 		auto computeLength = [&](const std::vector<GVector2D>& A)
// 		{
// 			float l = 0;
// 			for (int i = 1; i < A.size(); ++i)
// 				l += distance(A[i - 1], A[i]);
// 			return l;
// 		};

// 		auto lengthSort = [&](const std::vector<GVector2D>& A, const std::vector<GVector2D>& B) {return computeLength(A) < computeLength(B); };
// 		std::ranges::sort(lines, lengthSort);

		return lines;
    }

    void CliffCluster::computeFaultParameters()
    {
		auto&& diagram = Data::get()->getTerrainCells();
		auto&& dem = Data::get()->getDEM();

		// Compute bot and top heights
		float maxH = std::numeric_limits<float>::min();
		float minH = std::numeric_limits<float>::max();
		for (auto&& p : *borderPoints.find(keyCell))
		{
			float h = dem->heightData.sample(p.pos);
			minH = std::min(h, minH);
			maxH = std::max(h, maxH);
		}

		static std::uniform_real_distribution<float> heightRangeDist(0.9, 0.95);
		float faultHeight = (maxH - minH) * heightRangeDist(gRandomEngine);
		float midH = (maxH + minH) * 0.5f;
		botH = midH - faultHeight * 0.5f;
		topH = midH + faultHeight * 0.5f;
    }

    void CliffCluster::computeFaultSegments()
    {
		auto&& heightData = Data::get()->getDEM()->heightData;

		auto&& mainLine = calculateFaultMainLine();
		std::reverse(mainLine.begin(), mainLine.end());

		// remove if line is too small
		float mainLineDist = 0.0f;
		for (int i = 0; i < (int)mainLine.size() - 1; i++)
			mainLineDist += mainLine[i].dist(mainLine[i + 1]);

		if (mainLineDist < 200.0f)
			return;

		float maxFaultWidth = 40.0f * std::max((topH - botH) / 50.0f, 1.0f);

		auto calculateFaultLine = [&](bool towardGradient)
		{
			std::vector<GVector2D> faultLine;
			auto&& gradientDirection = heightData.sampleGradient(mainLine[mainLine.size() * 0.5f]).normalized() * (towardGradient ? 1.0f : -1.0f);

			for (int i = 0; i < mainLine.size(); i++)
			{
				float distToEdge = 0.0f;

				auto&& polygonIntersection = clusterPolygon.rayIntersections({ mainLine[i], mainLine[i] + gradientDirection * 100000.0f });
				Q_ASSERT(!polygonIntersection.empty());

				// Intention here is to avoid case where fault line is behind concave point
				if (auto&& concaveIntersection = findIntersectionWithConcaveSegment(mainLine, i, gradientDirection); concaveIntersection)
					distToEdge = std::min(mainLine[i].dist(*concaveIntersection), polygonIntersection.firstKey());
				else
					distToEdge = polygonIntersection.firstKey();

				auto&& t = (float)i / mainLine.size();
				auto distToMove = std::min(maxFaultWidth * (1.0f - std::abs(t - 0.5f)), distToEdge * 0.5f);
				faultLine.push_back(mainLine[i] + gradientDirection * distToMove);
			}

			return faultLine;
		};

		std::vector<GVector2D> botLine = calculateFaultLine(false);
		std::vector<GVector2D> topLine = calculateFaultLine(true);

		snapToBorderPoints(&botLine, &topLine, mainLine);
		auto&& [botFrontIdx, botBackIdx] = fixSnappedLine(&botLine, 50.0f);
		auto&& [topFrontIdx, topBackIdx] = fixSnappedLine(&topLine, 50.0f);

		botLine = BezierCurve2D(botLine).getPoints(botLine.size() - 1);
		topLine = BezierCurve2D(topLine).getPoints(topLine.size() - 1);

		applyNoise(&botLine, botFrontIdx, botBackIdx, mainLine);
		applyNoise(&topLine, topFrontIdx, topBackIdx, mainLine);

		// Generate between lines (wall lines)
		std::vector<std::vector<GVector2D>> betweenLines;

		int amountOfLines = 5; // (topH - botH) / 10.0f;
		std::vector<float> distanceDiffPerLine(mainLine.size(), 0.1f);
		for (int line = 1; line < amountOfLines; line++)
		{
			std::vector<GVector2D> betweenLine;

			for (int i = 0; i < mainLine.size(); i++)
			{
				distanceDiffPerLine[i] = 0.0f; // std::uniform_real_distribution(distanceDiffPerLine[i], std::min(distanceDiffPerLine[i] + 0.3f, 0.9f))(Generation::gRandomEngine);
				auto&& dist = botLine[i].dist(topLine[i]);
				auto&& dir = botLine[i] + (topLine[i] - botLine[i]) * distanceDiffPerLine[i];

				betweenLine.push_back(dir);
			}

			auto&& lineToSnap = (line < amountOfLines * 0.5f) ? botLine : topLine;
			betweenLine.front() = lineToSnap.front();
			betweenLine.back() = lineToSnap.back();
			betweenLines << BezierCurve2D(betweenLine).getPoints(betweenLine.size() - 1);
		}

		faultSegments << botLine /* << betweenLines */<< topLine;

#if 0
		{
			std::vector<QVector3D> line;
			for (auto&& pt : mainLine)
				line << QVector3D(pt.x, heightData.sample(pt), pt.z);

			spawn<DLineMarker>(line, QVector4D(1, 0, 0, 1));
		}

		{
			std::vector<QVector3D> line;
			for (auto&& pt : faultSegments.front())
				line << QVector3D(pt.x, botH, pt.z);

			spawn<DLineMarker>(line, QVector4D(0, 1, 0, 1));
		}
		{
			std::vector<QVector3D> line;
			for (auto&& pt : faultSegments.back())
				line << QVector3D(pt.x, topH, pt.z);

			spawn<DLineMarker>(line, QVector4D(0, 0, 1, 1));
		}

		{
			for (int i = 1; i < faultSegments.size() - 1; i++)
			{
				std::vector<QVector3D> line;
				for (auto&& pt : faultSegments[i])
					line << QVector3D(pt.x, heightData.sample(pt), pt.z);

				spawn<DLineMarker>(line, QVector4D(1, 0, 1, 1));
			}
		}
#endif
    }

	std::optional<GVector2D> CliffCluster::findHeightMidPoint()
	{
		auto&& heightData = Data::get()->getDEM()->heightData;

		float minH = std::numeric_limits<float>::max(), maxH = std::numeric_limits<float>::min();
		GVector2D minPt;
		GVector2D maxPt;

		for (auto&& pt : clusterBorderPoints)
		{
			auto height = heightData.sample(pt);

			if (minH > height)
			{
				minH = height;
				minPt = pt;
			}
			if (maxH < height)
			{
				maxH = height;
				maxPt = pt;
			}
		}

		if (minPt == maxPt)
			return std::nullopt;

		std::optional<GVector2D> midPoint;
		float biggestHorizontalDist = 0.0f;

		float maxDistance = minPt.dist(maxPt);
		GVector2D toMaxDir = (maxPt - minPt).normalized();
		GVector2D nextPt = minPt + toMaxDir * 100.f;

		// look for mid point with most space to spread
		for (float d = 0.0f; d < maxDistance; d += 100.0f)
		{
			auto&& gradient = heightData.sampleGradient(nextPt).normalized();
			gradient = gradient != GVector2D(0, 0) ? gradient : toMaxDir;

			auto ptForRay = nextPt + gradient.rotatedLeft90() * 100000.0f;
			auto&& intersections = clusterPolygon.rayIntersections({ ptForRay, ptForRay + gradient.rotatedRight90() * 1000000.0f });
			auto&& keys = intersections.keys();

			for (int i = 0; i < keys.size() - 1; i++)
			{
				auto dist = keys[i + 1] - keys[i];
				auto midPt = (std::get<GVector2D>(intersections[keys[i]]) + std::get<GVector2D>(intersections[keys[i + 1]])) * 0.5f;
				auto isZeroGradient = heightData.sampleGradient(midPt) == GVector2D(0, 0);
				auto tooCloseToPolygon = clusterPolygon.getRadiusOfInscribedCircleAtPoint(midPt) < 100.0f;

				if (biggestHorizontalDist < dist && clusterPolygon.containsConcave(midPt) && !isZeroGradient && !tooCloseToPolygon)
				{
					biggestHorizontalDist = dist;
					midPoint = midPt;
				}
			}

			nextPt += toMaxDir * 100.f;
		}

		return midPoint;
	}

	std::vector<GVector2D> CliffCluster::calculateFaultMainLine()
	{
		auto&& heightData = Data::get()->getDEM()->heightData;

		auto&& midPoint = findHeightMidPoint();

		if (!midPoint)
			return {};

		auto findGradientPathInsidePolygon = [&](const GVector2D& startPoint, bool rotateLeft)
		{
			std::vector<GVector2D> path;
			std::vector<float> angleDelta;
			const float moveDist = 10.0f;
			const float angleDistControl = 1000.0f;
			const float angleValueControl = 20.0f;

			GVector2D nextPoint = startPoint;
			GVector2D gradient = heightData.sampleGradient(nextPoint).normalized();
			gradient = rotateLeft ? gradient.rotatedLeft90() : gradient.rotatedRight90();
			GVector2D prevGradient = gradient;

			while (true)
			{
				nextPoint += gradient * moveDist;
				auto&& nextGradient = heightData.sampleGradient(nextPoint).normalized();
				nextGradient = rotateLeft ? nextGradient.rotatedLeft90() : nextGradient.rotatedRight90();
				nextGradient = nextGradient != GVector2D(0, 0) ? nextGradient : prevGradient;

				// adjust angle by amount of previous deltas to not pass control value
				auto angle = angle180S(gradient, nextGradient);

				int fromDelta = std::max((int)((angleDelta.size() * moveDist - angleDistControl) / moveDist), 0);
				auto accAngle = std::accumulate(angleDelta.begin() + fromDelta, angleDelta.end(), 0.0f);
				if (std::abs(accAngle + angle) > angleValueControl)
					angle = (accAngle > 0 ? 1.0f : -1.0f) * angleValueControl - accAngle;

				gradient = GVector2D::rotateDegrees(gradient, angle);

				// in case of path moving close and in direction of cocave point, end path (better solution might be discovered)
				for (int i = 0; i < clusterPolygon.getPts().size(); i++)
					if (clusterPolygon.isConcaveVertex(i) && clusterPolygon[i].dist(nextPoint) < 100)
					{
						auto angleToConcave = angle180S(gradient, (clusterPolygon[i] - nextPoint).normalized());
						if (std::abs(angleToConcave) < 60)
							return path;

					}

				// end path on intersection / if point is outside of polygon
				if (auto&& intersections = clusterPolygon.rayIntersections({ path.empty() ? *midPoint : path.back(), nextPoint }); !intersections.empty() || !clusterPolygon.containsConcave(nextPoint))
					break;

				path << nextPoint;
				angleDelta << angle;
				prevGradient = gradient;
			}

			return path;
		};

		std::vector<GVector2D> leftSide = findGradientPathInsidePolygon(*midPoint, true);
		std::vector<GVector2D> rightSide = findGradientPathInsidePolygon(*midPoint, false);
		std::reverse(leftSide.begin(), leftSide.end());

		return leftSide << *midPoint << rightSide;
	}

	std::optional<GVector2D> CliffCluster::lookForConcavePoint(const std::vector<GVector2D>& mainLine, int idx, const GVector2D& direction)
	{
		if (idx == 0 || idx == mainLine.size() - 1)
			return std::nullopt;

		auto findPolygonPartForSearch = [&]()
		{
			auto&& betweenIntersections = clusterPolygon.rayIntersections({ mainLine[idx], mainLine[idx] + direction * 100000.0f });
			Q_ASSERT(!betweenIntersections.empty());

			auto&& startIntersections = clusterPolygon.rayIntersections({ mainLine[0], mainLine[0] + direction * 100000.0f });
			Q_ASSERT(!startIntersections.empty());

			auto&& endIntersections = clusterPolygon.rayIntersections({ mainLine[mainLine.size() - 1], mainLine[mainLine.size() - 1] + direction * 100000.0f });
			Q_ASSERT(!endIntersections.empty());

			auto&& [betweenPt, _] = betweenIntersections.begin().value();
			auto&& [startPt, startIndexes] = startIntersections.begin().value();
			auto&& [endPt, endIndexes] = endIntersections.begin().value();

			auto&& cPts = clusterPolygon.getCPts();
			int winding = 0;

			auto prevPt = startPt;
			for (int i = 0; i < cPts.getSize() + 1; i++)
			{
				auto nextPt = cPts[cPts.findIdx(startIndexes[1], i)];

				Segment2D testSeg(prevPt, nextPt);
				auto distToEnd = prevPt.dist(endPt);
				auto distToBetween = prevPt.dist(betweenPt);

				if (testSeg.dist(endPt) < 1.0f && !(testSeg.dist(betweenPt) < 1.0f && distToBetween < distToEnd))
				{
					winding = -1;
					break;
				}
				else if (testSeg.dist(betweenPt) < 1.0f)
				{
					winding = 1;
					break;
				}

				prevPt = nextPt;
			}

			Q_ASSERT(winding != 0);
			int start = winding > 0 ? startIndexes[0] : endIndexes[0];
			int end = winding > 0 ? endIndexes[1] : startIndexes[1];

			return std::pair<int, int>{ start, end };
		};

		auto&& cPts = clusterPolygon.getCPts();
		auto&& [startIdx, endIdx] = findPolygonPartForSearch();

		std::optional<GVector2D> concavePoint;
		float closestConcave = std::numeric_limits<float>::max();

		int nextIdx = startIdx;
		while (true)
		{
			auto&& dist = mainLine[idx].dist(clusterPolygon[nextIdx]);
			if (clusterPolygon.isConcaveVertex(nextIdx) && closestConcave > dist)
			{
				concavePoint = clusterPolygon[nextIdx];
				closestConcave = dist;
			}

			if (nextIdx == endIdx)
				break;

			nextIdx = cPts.findIdx(nextIdx, 1);
		}

		return concavePoint;
	}

	std::optional<GVector2D> CliffCluster::findIntersectionWithConcaveSegment(const std::vector<GVector2D>& mainLine, int idx, const GVector2D& direction)
	{
		if (auto&& concavePoint = lookForConcavePoint(mainLine, idx, direction); concavePoint)
		{
			auto&& lineDirection = (mainLine[idx - 1] - mainLine[idx + 1]).normalized();
			Segment2D segment(*concavePoint + lineDirection * 100000.0f, *concavePoint - lineDirection * 100000.0f);

			return segment.getIntersectionPoint({ mainLine[idx], mainLine[idx] + direction * 100000.0f });
		}

		return std::nullopt;
	}

	void CliffCluster::computeClusterBorderPoints()
	{
		if (!clusterBorderPoints.empty())
			return;

		// Compute own border points
		auto&& cPts = clusterPolygon.getCPts();
		for (int i = 0; i < cPts.getSize(); ++i)
		{
			int i2 = cPts.findIdx(i, 1);
			clusterBorderPoints << splitSegment(Segment2D{ cPts[i], cPts[i2] }, FFirstLastPolicy::First, true);
		}
	}

	std::pair<int, int> CliffCluster::fixSnappedLine(std::vector<GVector2D>* line, float acceptableAngle)
	{
		// Find index where line direction and line to snap point are within acceptable angle
		std::pair<int, int> indexesToSnap;

		for (int i = 0; i < line->size() - 2; i++)
		{
			auto&& toEndDir = (line->front() - (*line)[i + 1]).normalized();
			auto&& lineDir = ((*line)[i + 1] - (*line)[i + 2]).normalized();

			if (angle180(lineDir, toEndDir) < acceptableAngle)
			{
				indexesToSnap.first = i + 1;
				break;
			}
		}

		for (int i = line->size() - 1; i > 1; i--)
		{
			auto&& toEndDir = (line->back() - (*line)[i - 1]).normalized();
			auto&& lineDir = ((*line)[i - 1] - (*line)[i - 2]).normalized();

			if (angle180(lineDir, toEndDir) < acceptableAngle)
			{
				indexesToSnap.second = i - 1;
				break;
			}
		}

		auto straightenLine = [](std::vector<GVector2D>* line, int from, int to)
		{
			auto&& dir = ((*line)[to] - (*line)[from]).normalized();
			auto&& dist = (*line)[from].dist((*line)[to]);
			int diff = to - from;

			for (int i = 1; i < diff; i++)
				(*line)[from + i] = (*line)[from] + dir * dist * (i / (float)diff);
		};

		straightenLine(line, 0, indexesToSnap.first);
		straightenLine(line, indexesToSnap.second, line->size() - 1);
		return indexesToSnap;
	}

	void CliffCluster::snapToBorderPoints(std::vector<GVector2D>* botLine, std::vector<GVector2D>* topLine, const std::vector<GVector2D>& mainLine)
	{
		Polygon2D borderPolygon(clusterBorderPoints);

		auto findPointsToSnapTo = [&](const GVector2D& point)
		{
			auto&& edges = borderPolygon.getClosestEdges(point);
			auto&& [i1, i2, pt] = edges.front();

			return std::pair(borderPolygon[i1], borderPolygon[i2]);
		};

		std::tie(topLine->front(), botLine->front()) = findPointsToSnapTo(mainLine.front());
		std::tie(botLine->back(), topLine->back()) = findPointsToSnapTo(mainLine.back());
	}

	float getNoiseCliff(float x, float z)
	{
		static std::mt19937 randomEngine;
		static noise::module::RidgedMulti noiseSource;
		static noise::model::Plane noiseModel;
		static std::atomic_bool isInited = false;

		if (!isInited)
		{
			static std::mutex guard;
			std::scoped_lock lock(guard);
			noiseSource.SetSeed(randomEngine());
			noiseSource.SetFrequency(2e-3f);
			noiseSource.SetOctaveCount(2);
			noiseModel.SetModule(noiseSource);
			isInited = true;
		}

		return noiseModel.GetValue(x, z);
	}

	void CliffCluster::applyNoise(std::vector<GVector2D>* line, int from, int to, const std::vector<GVector2D>& mainLine)
	{
		// Reduce noise on edges of line
		auto edgeNoiseReduction = [](int i, int size, float modifier)
		{
			return std::clamp((1.0f - std::abs(size * 0.5f - i) / (size * 0.5f)) / modifier, 0.0f, 1.0f);
		};

		for (int i = from; i < to; i++)
		{
			auto dir = (mainLine[i] - (*line)[i]).normalized();
			auto dist = mainLine[i].dist((*line)[i]);
			(*line)[i] += dir * dist * edgeNoiseReduction(i - from, to, 0.5f) * std::lerp(0.15f, 0.35f, std::abs(getNoiseCliff((*line)[i].x, (*line)[i].z)));
		}
	}

	void CliffCluster::initialize()
	{
		clusterPolygon = calculatePolygon(true);

		// Border points / block
		computeClusterBorderPoints();
		computeFaultParameters();
		computeFaultSegments();

		smoothingParams.weight = 1.0f;
	}

	void CliffCluster::meshVerticalSection(const std::vector<std::vector<GVector2D>>& lines, const std::array<std::array<IndexType, 2>, 2>& outers,
		std::vector<QVector3D>* generatedPoints, std::vector<IndexType>* generatedTriangles, std::unordered_set<GVector2D>* verticalSectionVertices)
	{
		auto&& botLine = lines.front();
		auto&& topLine = lines.back();

		std::vector<std::vector<IndexType>> lineIndices;
		lineIndices.reserve(lines.size());
		for (auto&& line : lines)
			lineIndices << std::vector<IndexType>(line.size(), -1);

		for (int i = 0; i < botLine.size(); ++i)
		{
            for (int pIdx = outers[0][0]; pIdx < outers[0][1]; ++pIdx)
                if (vEq(GVector2D((*generatedPoints)[pIdx]), botLine[i]))
                {
                    lineIndices.front()[i] = pIdx;
                    break;
                }

            for (int pIdx = outers[1][0]; pIdx < outers[1][1]; ++pIdx)
                if (vEq(GVector2D((*generatedPoints)[pIdx]), topLine[i]))
                {
                    lineIndices.back()[i] = pIdx;
                    break;
                }

			Q_ASSERT(lineIndices.front()[i] < generatedPoints->size());
			Q_ASSERT(lineIndices.back()[i] < generatedPoints->size());
		}

		auto&& assets = QOmnigenAssetMgrSection::getAssets<EAsset::RockMaterial>();
		auto&& rockArray = Data::get()->getTerrainTextureArray();
		auto&& rock = assets.at(rockArray[metaCluster->getTerrainTexPack()]);
		auto&& cliffMaterial = rock->getTextures()[3];
		auto&& tex = cliffMaterial.outputs.at(ETextureComponentOut::DiffuseHeight);

		gli::fsampler2D sampler(tex.getData(), gli::wrap::WRAP_REPEAT);
		QSize size = { tex.getData().extent().x, tex.getData().extent().y };
		const float coordScale = 0.1f;
		float maxDisplacement = 100.0f;

// 		for (int l = 0; l < int(lines.size()); ++l)
// 		{
// 			float h = std::lerp(botH, topH, float(l) / float(int(lines.size()) - 1));
// 			spawn<DLineMarker>(lines[0], Colors::random(), false, h);
// 		}

		// Add intermediate lines' points
		for (int l = 1; l < int(lines.size()) - 1; ++l)
		{
			float h = std::lerp(botH, topH, float(l) / float(int(lines.size()) - 1));
			float yCoord = topH - h;

			float xCoord = 0.0f;
			for (int pIdx = 0; pIdx < lines[l].size(); ++pIdx)
			{
				if (pIdx > 0)
					xCoord += distance(lines[l][pIdx - 1], lines[l][pIdx]);

				GVector2D p = lines[l][pIdx];

				if (false && pIdx > 0 && pIdx < lines[l].size() - 1)
				{
					gli::ivec2 coords;
					coords.x = std::fmodf(xCoord * coordScale, float(size.width()));
					coords.y = std::fmodf(yCoord * coordScale, float(size.width()));

					auto color = sampler.texel_fetch(gli::texture2d::extent_type(coords), 0);

					GVector2D normal;
					if (pIdx == 0)
						normal = (lines[l][pIdx + 1] - p);
					else if (pIdx == lines[l].size() - 1)
						normal = (p - lines[l][pIdx - 1]);
					else
						normal = (lines[l][pIdx + 1] - lines[l][pIdx - 1]);

					normal = normal.rotatedLeft90().normalized();
					p += normal * color.a * maxDisplacement;
				}

				lineIndices[l][pIdx] = generatedPoints->size();
				generatedPoints->push_back({ p.x, h, p.z });
				verticalSectionVertices->insert((GVector2D)generatedPoints->back());
			}
		}

		// Add quads
		for (int l = 1; l < lines.size(); ++l)
			for (int i = 1; i < botLine.size(); ++i)
			{
				quint32 BL = lineIndices[l - 1][i - 1];
				quint32 TL = lineIndices[l][i - 1];
				quint32 TR = lineIndices[l][i];
				quint32 BR = lineIndices[l - 1][i];

				// 2 triangles
				(*generatedTriangles) << BL << TL << TR;
				(*generatedTriangles) << BL << TR << BR;
			}
	}

	void CliffCluster::meshCluster(MeshConnector* meshConnector, std::unordered_set<GVector2D>* verticalSectionVertices)
	{
        static auto rotateRight90 = QQuaternion::fromEulerAngles(0, -90, 0);

        Polygon2D detailedArea(clusterBorderPoints);

        // Flat areas
        auto [bottom, bottomLeftovers] = splitPolygonByFittedMultiLine(detailedArea, faultSegments.front());
        auto [topLeftovers, top] = splitPolygonByFittedMultiLine(detailedArea, faultSegments.back());

        GeometryData<GVector2D> botGeom;
        auto& botVertices = botGeom.vertices;
        auto& botIndices = botGeom.indices;
        IndexType botOuterCount;

        GeometryData<GVector2D> topGeom;
        auto& topVertices = topGeom.vertices;
        auto& topIndices = topGeom.indices;
        IndexType topOuterCount;

        if (bottom.getPts().size() > 2)
            std::tie(botGeom, botOuterCount) = meshPolygon2(bottom.getPts());

        if (top.getPts().size() > 2)
            std::tie(topGeom, topOuterCount) = meshPolygon2(top.getPts());

        std::vector<QVector3D> generatedPoints;
        std::vector<IndexType> generatedTriangles;
        generatedPoints.reserve(botVertices.size() + topVertices.size());
        generatedTriangles.reserve(botIndices.size() + topIndices.size());

        for (int i = 0; i < botVertices.size(); ++i)
        {
            auto&& p = botVertices[i];
            generatedPoints << QVector3D{ p.x, botH, p.z };
        }
        for (int i = 0; i < topVertices.size(); ++i)
        {
            auto&& p = topVertices[i];
            generatedPoints << QVector3D{ p.x, topH, p.z };
        }

        generatedTriangles << botIndices;
        for (int i : topIndices)
            generatedTriangles << botVertices.size() + i;

        std::array<std::array<IndexType, 2>, 2> outers;
        outers[0][0] = 0;
        outers[0][1] = botOuterCount;
        outers[1][0] = botVertices.size();
        outers[1][1] = botVertices.size() + topOuterCount;

        // Vertical segments
        if (botVertices.size() > 0 && topVertices.size() > 0)
            meshVerticalSection(faultSegments, outers, &generatedPoints, &generatedTriangles, verticalSectionVertices);

        // Enviro bounds
        auto lower = QSharedPointer<EnvBound>::create(keyCell);
        auto upper = QSharedPointer<EnvBound>::create(keyCell);
        lower->line = { faultSegments.front().begin(), faultSegments.front().end() };
        upper->line = { faultSegments.back().begin(), faultSegments.back().end() };
        for (int i = 0; i < lower->line.size(); ++i)
        {
            lower->line[i].setY(botH);
            upper->line[i].setY(topH);
        }

        lower->pairedBounds << upper;
        upper->pairedBounds << lower;
        upper->flipDir = true;

        auto frontSide = QSharedPointer<EnvBound>::create(keyCell);
        auto backSide = QSharedPointer<EnvBound>::create(keyCell);
        frontSide->line = { *upper->line.begin(), *lower->line.begin() };
        backSide->line = { *lower->line.rbegin(), *upper->line.rbegin() };

        Data::get()->addEnviroBound(lower);
        Data::get()->addEnviroBound(upper);
        Data::get()->addEnviroBound(frontSide);
        Data::get()->addEnviroBound(backSide);

        meshConnector->addMesh3D(generatedPoints, generatedTriangles);
	}

//     void CliffCluster::generateMesh()
//     {
// 		auto lambda = [&]() -> resumable::RetVal
// 		{
// 			auto&& heightData = Data::get()->getDEM()->heightData;
// 
// 			auto lines = makeLines();
// 			QVector3D color = Colors::random().toVector3D().normalized();
// 			for (auto&& line : lines)
// 			{
// 				float h = heightData.sample(line[0]);
// 				QVector4D c(color * (h - botH) / (topH - botH), 1);
// 				//spawn<DLineMarker>(line, c, false, 100);
// 				//spawn<DLineMarker>(line, QVector4D{ 1,1,1,0.2 }, false, 0);
// 			}
// 
// 			Polygon2D basePoly(clusterBorderPoints);
// 			std::vector<Polygon2D> remainingShapes = { basePoly };
// 			std::vector<Polygon2D> outputPolys;
// 			std::vector<QSharedPointer<DLineMarker>> markers;
// 
// 
// 
// 			while (true)
// 			{
// 				for (auto&& marker : markers)
// 					Data::get()->clearSingleExactMarker<DLineMarker>(marker->getGuid());
// 
// 				std::vector<Polygon2D> newRemainingShapes;
// 
// 				for (auto&& remainingShape : remainingShapes)
// 				{
// 					// Extract the minimal new part from remaining lines
// 					std::vector<Polygon2D> newPart = { remainingShape };
// 					for (int l = 0; l < lines.size(); ++l)
// 					{
// // 						for (auto&& marker : markers)
// // 							Data::get()->clearSingleExactMarker<DLineMarker>(marker->getGuid());
// 
// 						// Get the lower polygon by the line
// 						auto [partA, partB] = splitPolygonByPointFittedMultiLine(basePoly, lines[l]);
// 						float hA = heightData.sample(partA.getCenter());
// 						float hB = heightData.sample(partB.getCenter());
// 						auto* cutPart = (hA < hB) ? &partA : &partB;
// 						auto* remainingPart = (hA < hB) ? &partB : &partA;
// 
// 						// Check if it's part of the area of interest
// 						auto commonPart = Polygon2D::boolOp(newPart[0], *cutPart, Polygon2D::EBoolOp::Intersection);
// 						if (commonPart.empty() || commonPart[0].getPts() == newPart[0].getPts())
// 							continue;
// 
// 						//markers << spawn<DLineMarker>(commonPart[0].getPts(), Colors::yellow, true, 40);
// 
// 						// Update new part
// 						newPart = commonPart;
// 
// 						// Line processed, remove it.
// 						//markers << spawn<DLineMarker>(lines[l], QVector4D{ 1,0,0,1 }, false, 20);
// 						lines.erase(lines.begin() + l--);
// 
// 						//markers << spawn<DLineMarker>(cutPart->getCenter(), 1000, Colors::green);
// 						//co_await resumable::Awaiter::get();
// 					}
// 
// 					// Store and update what's left
// 					outputPolys << newPart[0];
// 					//markers << spawn<DLineMarker>(newPart[0].getPts(), Colors::green, true, 0);
// 
// 					newRemainingShapes << Polygon2D::boolOp(remainingShape, newPart[0], Polygon2D::EBoolOp::Difference);
// 				}
// 
// 				if (newRemainingShapes.empty())
// 					break;
// 
// 				remainingShapes = newRemainingShapes;
// // 				for(auto&& shape : remainingShapes)
// // 					markers << spawn<DLineMarker>(shape.getPts(), Colors::red, true, 20);
// 
// 				//co_await resumable::Awaiter::get();
// 			}
// 
// 			for (auto&& line : lines)
// 				spawn<DLineMarker>(line, QVector4D{ 1,0,1,1 }, false, 0);
// 
// 			for (auto&& poly : outputPolys)
// 			{
// 				float h = heightData.sample(poly.getCenter());
// 				QVector4D c(color * (h - botH) / (topH - botH), 1);
// 				spawn<DLineMarker>(poly.getPts(), c, true, h);
// 			}
// 
// 			return {};
// 		}();
// 
// 		if (faultSegments.empty())
// 		{
// 			auto&& dem = Data::get()->getDEM();
// 			const auto [verts, indices, _] = meshPolygon(calculatePolygon());
// 
// 			GeometryData<TerrainMeshVertex> geometry;
// 			geometry.vertices.reserve(verts.size());
// 			for (auto&& vert : verts)
// 				geometry.vertices.push_back({ {vert.x, dem->heightData.sample(vert), vert.z}, {}, *this });
// 			geometry.indices = std::move(indices);
// 			return;
// 		}
// 
// 		MeshConnector meshConnector;
// 		std::unordered_set<GVector2D> verticalSectionVertices;
// 		verticalSectionVertices.reserve(100);
// 
// 		meshCluster(&meshConnector, &verticalSectionVertices);
// 
// 		meshConnector.indices.shrink_to_fit();
// 		GeometryData<TerrainMeshVertex> geometry;
// 		geometry.indices = std::move(meshConnector.indices);
// 		geometry.vertices.reserve(meshConnector.vertices.size());
// 		for (const auto& pt : meshConnector.vertices)
// 		{
// 			TerrainMeshVertex tmv{ pt, {}, *this };
// 			geometry.vertices <<= std::move(tmv);
// 			if (verticalSectionVertices.contains((GVector2D)pt))
// 				geometry.vertices.back().biomeTexWeights = 0;
// 		}
//     }

	QSharedPointer<BatchedSection<ClusterMeshBatchParams>> CliffCluster::generateMesh()
	{
		auto&& dem = Data::get()->getDEM();

		auto&& area = calculatePolygon();

		static auto getCliffSegments = [](const GVector2D& p1, const GVector2D& p2)
		{
			auto&& dem = Data::get()->getDEM();
			QVector3D P1 = { p1.x, dem->heightData.sample(p1), p1.z };
			QVector3D P2 = { p2.x, dem->heightData.sample(p2), p2.z };
			const float dist = distance(P1, P2);
			return dist / 10.0f;
		};

		static auto params = []()
		{
			MeshingParams meshingParams = getDefaultMeshingParams();
			meshingParams.innerSplitFunc = [](const GVector2D& v1, const GVector2D& v2, FFirstLastPolicy inclusionPolicy)
			{
				return splitSegment(Segment2D{ v1, v2 }, inclusionPolicy, true, getCliffSegments(v1, v2));
			};
			return meshingParams;
		}();

		auto [geom2D, outers] = meshPolygon2(area.getPts(), params);
		auto& verts = geom2D.vertices;
		auto& indices = geom2D.indices;

		GeometryData<TerrainMeshVertex> geometry;
		geometry.vertices.reserve(verts.size());
		for (auto&& v : verts)
		{
			TerrainMeshVertex finalPoint = { {v.x, dem->heightData.sample(v), v.z}, {}, *this };
			setPackParam(&finalPoint.packParams, 0, 0.0f);
			geometry.vertices << finalPoint;
		}

		geometry.indices = std::move(indices);

		computeNormalsDuringMeshGen();

		auto&& assets = QOmnigenAssetMgrSection::getAssets<EAsset::RockMaterial>();
		auto&& rockArray = Data::get()->getTerrainTextureArray();
		auto&& rock = assets.at(rockArray[metaCluster->getTerrainTexPack()]);
		auto&& cliffMaterial = rock->getTextures()[3];
		auto&& tex = cliffMaterial.outputs.at(ETextureComponentOut::DiffuseHeight);

		gli::fsampler2D sampler(tex.getData(), gli::wrap::WRAP_REPEAT);
		QSize size = { tex.getData().extent().x, tex.getData().extent().y };

		for (int i = outers; i < geometry.vertices.size(); ++i)
		{
			auto&& v = geometry.vertices[i];

			float xCoord = dem->verticalDisplacementXCoords.sample(v.position);
			Q_ASSERT(xCoord >= 0.0f);
			float tileSize = 100'00.0f;
			float yCoord = fmodf(v.position.y(), tileSize) / tileSize;

			gli::ivec2 coords;
			coords.x = std::round(xCoord * float(size.width()));
			coords.y = std::round(yCoord * float(size.height()));

			auto color = sampler.texel_fetch(gli::texture2d::extent_type(coords), 0);
			v.position += v.normal * (color.a * 1000.0f);
			setPackParam(&v.packParams, 3, xCoord);
		}

		return spawnBatched(std::move(geometry), makeBatchParams());
	}

    // distance to shoreline with sign determining the side on which the point lies
    float CliffCluster::shorelineDistance(const GVector2D& point) const
    {
        auto&& qtree = Data::get()->getMarkerQuadTree<DShorelineMarker>();
        float lookupDistance = 2 * calculatePolygon().getRadius();
        auto nodes = qtree.find_all_nearest(point.x, point.z, lookupDistance);

        float minD = std::numeric_limits<float>::max();
        LineMarkerPoint closestPoint;

        for (auto&& node : nodes)
        {
            GVector2D p(node->x, node->y);
            if (float d = distance(point, p); d < minD)
            {
                minD = d;
                closestPoint = node->data;
            }
        }

        int sign = getLineSide(closestPoint, point);
        return minD * sign;
    }

    void TerrainBlockMetaCluster<ETerrainBlock::Cliff>::computePackParams(const QSharedPointer<LithoCluster>& lithoCluster, const QSharedPointer<DDomain>& biomeDomain, float averageIHLevel)
    {
        // 100% rock slab, no vegetation
        packParams = 0;
    }

    void CliffCluster::computeNormalsDuringMeshGen()
    {
		// Triangles
		auto triangles = section->getIndices();
		auto&& vertexBuffer = section->mainBuffer->vertices;
        for (IndexType ti = 0; ti < triangles.size() / 3; ++ti)
        {
            IndexType i = ti * 3;

            auto&& mp1 = vertexBuffer[triangles[i]];
            auto&& mp2 = vertexBuffer[triangles[i + 1]];
            auto&& mp3 = vertexBuffer[triangles[i + 2]];

            QVector3D faceNormal = computeFaceNormal({ mp1.position, mp2.position, mp3.position });

            mp1.normal += faceNormal;
            mp2.normal += faceNormal;
            mp3.normal += faceNormal;
        }

        for (auto&& mp : section->getVertices())
		    mp.normal.normalize();
    }
}

void omniSave(const Generation::TerrainBlockCluster<Generation::ETerrainBlock::Cliff>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const Generation::TerrainBlockClusterBase&>(object);
}

void omniLoad(Generation::TerrainBlockCluster<Generation::ETerrainBlock::Cliff>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<Generation::TerrainBlockClusterBase&>(object);
}
