#pragma once

#include <vector>
#include <stdint.h>
#include "CoreUtils.h"
#include <QVector3D>

namespace PoissonGenerator
{
	class DefaultPRNG
	{
	public:
		DefaultPRNG() = default;
		explicit DefaultPRNG(unsigned int seed) :seed_(seed) {}
		inline float randomFloat()
		{
			seed_ *= 521167;
			uint32_t a = (seed_ & 0x007fffff) | 0x40000000;
			// remap to 0..1
			return 0.5f * (*((float*)&a) - 2.0f);
		}
		inline uint32_t randomInt(uint32_t maxInt)
		{
			return uint32_t(randomFloat() * maxInt);
		}
		inline uint32_t getSeed() const { return seed_; }
	private:
		uint32_t seed_ = 7133167;
	};

	struct Grid2D
	{
		Grid2D(int w, int h, float cellSize)
			: w_(w)
			, h_(h)
			, cellSize_(cellSize)
		{
			grid_.resize(h_);
			for (auto i = grid_.begin(); i != grid_.end(); i++) { i->resize(w); }
		}
		void insert(const GVector2D& p)
		{
			const GPoint g = imageToGrid(p, cellSize_);
			grid_[g.x][g.z] = p;
		}
		bool isInNeighbourhood(const GVector2D& point, float minDist, float cellSize)
		{
			const GPoint g = imageToGrid(point, cellSize);

			// number of adjucent cells to look for neighbour points
			const int D = 5;

			// scan the neighbourhood of the point in the grid
			for (int i = g.x - D; i <= g.x + D; i++)
			{
				for (int j = g.z - D; j <= g.z + D; j++)
				{
					if (i >= 0 && i < w_ && j >= 0 && j < h_)
					{
						auto P = grid_[i][j];

						if (P && distance(*P, point) < minDist)
							return true;
					}
				}
			}

			return false;
		}

	private:
		GPoint imageToGrid(const GVector2D& P, float cellSize)
		{
			return GPoint((int)(P.x / cellSize), (int)(P.z / cellSize));
		}

		int w_;
		int h_;
		float cellSize_;
		std::vector< std::vector<std::optional<GVector2D>> > grid_;
	};

	struct Grid3D
	{
		struct GPoint3D : std::array<int, 3>
		{
			int& x = _Elems[0];
			int& y = _Elems[1];
			int& z = _Elems[2];
		};

		Grid3D(int _x, int _y, int _z, float cellSize)
			: x(_x)
			, y(_y)
			, z(_z)
			, cellSize_(cellSize)
		{
			grid_.resize(x);
			for (auto&& X : grid_)
				if (X.resize(y); true)
					for (auto&& Y : X)
						Y.resize(z);
		}
		void insert(const QVector3D& p)
		{
			const auto g = imageToGrid(p, cellSize_);
			grid_[g.x][g.y][g.z] = p;
		}
		bool isInNeighbourhood(const QVector3D& point, float minDist, float cellSize)
		{
			const GPoint3D g = imageToGrid(point, cellSize);

			// number of adjucent cells to look for neighbour points
			const int D = 2;

			// scan the neighbourhood of the point in the grid
			for (int i = g.x - D; i <= g.x + D; i++)
			{
				for (int j = g.y - D; j <= g.y + D; j++)
				{
					for (int k = g.z - D; k <= g.z + D; k++)
					{
						if ((std::clamp(i, 0, x-1) != i) || (std::clamp(j, 0, y-1) != j) || (std::clamp(k, 0, z-1) != k))
							continue;

						auto& P = grid_[i][j][k];
						if (P && distance(*P, point) < minDist)
							return true;
					}
				}
			}

			return false;
		}

	private:
		GPoint3D imageToGrid(const QVector3D& P, float cellSize)
		{
			return GPoint3D{ int(P.x() / cellSize), int(P.y() / cellSize), int(P.z() / cellSize) };
		}

		int x, y, z;
		float cellSize_;
		std::vector< std::vector<std::vector<std::optional<QVector3D>>> > grid_;
	};

	auto popRandom(auto& points, auto& generator)
	{
		const int idx = generator.randomInt(static_cast<int>(points.size()) - 1);
		const auto p = points[idx];
		points[idx] = std::move(points.back());
		points.pop_back();
		return p;
	}

	bool defaultBoundFunc2D(const GVector2D& p)
	{
		return (std::clamp(p.x, 0.0f, 1.0f) == p.x) && (std::clamp(p.z, 0.0f, 1.0f) == p.z);
	}

	bool defaultBoundFunc3D(const QVector3D& p)
	{
		return (std::clamp(p.x(), 0.0f, 1.0f) == p.x()) && (std::clamp(p.y(), 0.0f, 1.0f) == p.y()) && (std::clamp(p.z(), 0.0f, 1.0f) == p.z());
	}

	auto generatePoissonPoints(
		auto generator,
		float minDist,
		std::function<bool(const GVector2D&)> boundFunc = &defaultBoundFunc2D,
		uint32_t sampleMaxRetries = 40
	)
	{
		using Point = GVector2D;
		std::vector<Point> samplePoints;
		std::vector<Point> processList;

		// create the grid
		const float cellSize = minDist / sqrt(2.0f);

		const int gridW = (int)ceil(1.0f / cellSize);
		const int gridH = (int)ceil(1.0f / cellSize);

		Grid2D grid(gridW, gridH, cellSize);

		Point firstPoint;
		do {
			firstPoint = Point(generator.randomFloat(), generator.randomFloat());
		} while (!boundFunc(firstPoint));

		// update containers
		processList.push_back(firstPoint);
		samplePoints.push_back(firstPoint);
		grid.insert(firstPoint);

		auto generateRandomPointAround = [](const Point& p, float minDist, auto& generator)
		{
			// start with non-uniform distribution
			const float R1 = generator.randomFloat();
			const float R2 = generator.randomFloat();

			// radius should be between MinDist and 2 * MinDist
			const float radius = minDist * (R1 + 1.0f);

			// random angle
			const float angle = 2 * 3.141592653589f * R2;

			// the new point is generated around the point (x, y)
			const float x = p.x + radius * cos(angle);
			const float y = p.z + radius * sin(angle);

			return Point{ x, y };
		};

#if POISSON_PROGRESS_INDICATOR
		size_t progress = 0;
#endif

		// generate new points for each point in the queue
		while (!processList.empty())
		{
#if POISSON_PROGRESS_INDICATOR
			// a progress indicator, kind of
			if ((samplePoints.size()) % 1000 == 0)
			{
				const size_t newProgress = 200 * (samplePoints.size() + processList.size()) / numPoints;
				if (newProgress != progress)
				{
					progress = newProgress;
					std::cout << ".";
				}
			}
#endif // POISSON_PROGRESS_INDICATOR

			const Point point = popRandom(processList, generator);

			for (uint32_t i = 0; i < sampleMaxRetries; i++)
			{
				const Point newPoint = generateRandomPointAround(point, minDist, generator);
				const bool canFitPoint = boundFunc(newPoint);

				if (canFitPoint && !grid.isInNeighbourhood(newPoint, minDist, cellSize))
				{
					processList.push_back(newPoint);
					samplePoints.push_back(newPoint);
					grid.insert(newPoint);
					continue;
				}
			}
		}

#if POISSON_PROGRESS_INDICATOR
		std::cout << std::endl << std::endl;
#endif // POISSON_PROGRESS_INDICATOR

		return samplePoints;
	}

	std::vector<QVector3D> generatePoissonPoints3D(
		auto& generator,
		float minDist,
		std::function<bool(const QVector3D&)> boundFunc = &defaultBoundFunc3D,
		uint32_t sampleMaxRetries = 30
	)
	{
		std::vector<QVector3D> samplePoints;
		std::vector<QVector3D> processList;

		// create the grid
		const float cellSize = minDist / sqrt(2.0f);

		const int gridX = (int)ceil(1.0f / cellSize);
		const int gridY = (int)ceil(1.0f / cellSize);
		const int gridZ = (int)ceil(1.0f / cellSize);

		Grid3D grid(gridX, gridY, gridZ, cellSize);

		QVector3D firstPoint;
		do {
			firstPoint = QVector3D(generator.randomFloat(), generator.randomFloat(), generator.randomFloat());
		} while (!boundFunc(firstPoint));

		// update containers
		processList.push_back(firstPoint);
		samplePoints.push_back(firstPoint);
		grid.insert(firstPoint);

		static auto generateRandomPointAround = [](const QVector3D& p, float minDist, auto& generator)
		{
			// start with non-uniform distribution
			const float RR = generator.randomFloat();
			QVector3D v = { generator.randomFloat() - 0.5f, generator.randomFloat() - 0.5f, generator.randomFloat() - 0.5f };
			while (v.isNull())
				v = { generator.randomFloat() - 0.5f, generator.randomFloat() - 0.5f, generator.randomFloat() - 0.5f };

			v.normalize();

			// radius should be between MinDist and 2 * MinDist
			const float radius = minDist * (RR + 1.0f);

			// the new point is generated around the point (x, y)
			return p + v * radius;
		};

#if POISSON_PROGRESS_INDICATOR
		size_t progress = 0;
#endif

		// generate new points for each point in the queue
		while (!processList.empty())
		{
#if POISSON_PROGRESS_INDICATOR
			// a progress indicator, kind of
			if ((samplePoints.size()) % 1000 == 0)
			{
				const size_t newProgress = 200 * (samplePoints.size() + processList.size()) / numPoints;
				if (newProgress != progress)
				{
					progress = newProgress;
					std::cout << ".";
				}
			}
#endif // POISSON_PROGRESS_INDICATOR

			const auto point = popRandom(processList, generator);

			for (uint32_t i = 0; i < sampleMaxRetries; i++)
			{
				const auto newPoint = generateRandomPointAround(point, minDist, generator);
				const bool canFitPoint = boundFunc(newPoint);

				if (canFitPoint && !grid.isInNeighbourhood(newPoint, minDist, cellSize))
				{
					processList.push_back(newPoint);
					samplePoints.push_back(newPoint);
					grid.insert(newPoint);
					continue;
				}
			}
		}

#if POISSON_PROGRESS_INDICATOR
		std::cout << std::endl << std::endl;
#endif // POISSON_PROGRESS_INDICATOR

		return samplePoints;
	}

	auto sampleVogelDisk(uint32_t idx, uint32_t numPoints, float phi)
	{
		using Point = GVector2D;

		const float kGoldenAngle = 2.4f;

		const float r = sqrtf(float(idx) + 0.5f) / sqrtf(float(numPoints));
		const float theta = idx * kGoldenAngle + phi;

		return Point(r * cosf(theta), r * sinf(theta));
	}

	/**
		Return a vector of generated points
	**/
	auto generateVogelPoints(uint32_t numPoints, bool isCircle = true, float phi = 0.0f, GVector2D center = GVector2D(0.5f, 0.5f))
	{
		using Point = GVector2D;
		std::vector<Point> samplePoints;

		samplePoints.reserve(numPoints);

		const uint32_t numSamples = isCircle ? 4 * numPoints : numPoints;

		for (uint32_t i = 0; i != numPoints; i++)
		{
			const Point p = sampleVogelDisk(i, numSamples, phi * 3.141592653f / 180.0f) + center;
			samplePoints.push_back(p);
		}

		return samplePoints;
	}

} // namespace PoissonGenerator
