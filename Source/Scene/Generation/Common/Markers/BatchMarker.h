#pragma once
#include "LineMarker.h"
#include "Utils/CoreUtils.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Editable.h"

template<typename BatchParams> struct BatchData;

namespace tml
{
	template<typename COOR_TYPE, typename DATA_TYPE> class qtree;
}

struct BufferHole
{
	IndexType offset;
	IndexType size;

	auto operator<=>(const BufferHole&) const noexcept = default;
};

template<typename T>
IndexType fitIntoBuffer(std::vector<T>&& object, std::list<BufferHole>* bufferHoles, std::vector<T>* buffer)
{
	BufferHole* bestFit = nullptr;
	for (auto&& hole : *bufferHoles)
		if (object.size() <= hole.size)
			if (!bestFit || (hole.size < bestFit->size))
				bestFit = &hole;

	IndexType resultOffset;
	if (bestFit)
	{
		// Sufficiently large gap found
		resultOffset = bestFit->offset;

		// Decrease gap width
		bestFit->offset += object.size();
		bestFit->size -= object.size();
		if (bestFit->size == 0)
		{
			// Remove hole
			std::swap(*bestFit, bufferHoles->back());
			bufferHoles->pop_back();
		}
	}
	else
	{
		// Append at the back
		resultOffset = buffer->size();
		buffer->resize(resultOffset + object.size());
	}

	// Move data, return final offset
	std::memmove(&(*buffer)[resultOffset], &object[0], object.size() * sizeof(T));
	return resultOffset;
}

struct BatchedSectionBase : public Editable
{
	BatchedSectionBase() : guid(makeGuid()) {};

	inline const auto& getGuid() const { return guid; }
	inline const auto& getVertexBufferOffset() const { return vertexBufferOffset; }
	inline const auto& getVertexBufferSize() const { return vertexBufferSize; }
	inline const auto& getIndexBufferOffset() const { return indexBufferOffset; }
	inline const auto& getIndexBufferSize() const { return indexBufferSize; }

	void setGuid(qint64 newGuid) { guid = newGuid; };
	void setVertexBufferOffset(IndexType newVertexBufferOffset) { vertexBufferOffset = newVertexBufferOffset; };
	void setVertexBufferSize(IndexType newVertexBufferSize) { vertexBufferSize = newVertexBufferSize; };
	void setIndexBufferOffset(IndexType newIndexBufferOffset) { indexBufferOffset = newIndexBufferOffset; };
	void setIndexBufferSize(IndexType newIndexBufferSize) { indexBufferSize = newIndexBufferSize; };

protected:
	qint64 guid;
	IndexType vertexBufferOffset, vertexBufferSize;
	IndexType indexBufferOffset, indexBufferSize;

	FRIEND_OMNIBIN(BatchedSectionBase);
};

template<typename BatchParams, bool ForInheritanceOnly = false>
struct BatchedSection : public BatchedSectionBase
{
	using VertexType = BatchParams::VertexType;

	BatchedSection() = default;
	BatchedSection(BatchParams inBatchParams, const QSharedPointer<GeometryDataBase>& inMainBuffer)
		: BatchedSectionBase()
		, batchParams(inBatchParams)
		, mainBuffer(inMainBuffer.staticCast<RenderGeometryData<typename BatchParams::VertexType>>())
	{}

	const auto& getBoundingBox() const
	{
		if (std::scoped_lock lock(boxGuard); cachedBoundingBox.sizes.isNull())
		{
			if constexpr (std::is_same_v<typename BatchParams::VertexType, QVector3D>)
			{
				cachedBoundingBox = BoundingBox::fromPoints(mainBuffer->vertices, [](auto&& p) { return p; }, vertexBufferOffset, vertexBufferSize);
			}
			else
			{
				cachedBoundingBox = BoundingBox::fromPoints(mainBuffer->vertices, [](auto&& p) { return p.position; }, vertexBufferOffset, vertexBufferSize);
			}
		}
		
		return cachedBoundingBox;
	}

	std::span<typename VertexType> getVertices() const
	{
        auto vertexBegin = mainBuffer->vertices.begin() + vertexBufferOffset;
        auto vertexEnd = vertexBegin + vertexBufferSize;
        return std::span(vertexBegin, vertexEnd);
	}

	std::span<IndexType> getIndices() const
	{
        auto faceBegin = mainBuffer->indices.begin() + indexBufferOffset;
        auto faceEnd = faceBegin + indexBufferSize;
        return std::span(faceBegin, faceEnd);
	}

	BatchParams getBatchParams() const
	{
		return batchParams;
	}

	void setGeometry(GeometryData<VertexType>&& newGeometry)
	{
		auto&& instance = gBatchingMarkerInstance<BatchParams>;
		auto&& instanceGuard = gBatchingMarkerInstanceGuard<BatchParams>;

		// Critical section #1: Instance
		if (std::scoped_lock lock(instanceGuard); !instance)
			instance = spawn<DBatchingMarker<BatchParams>>();

		//auto&& [batches, batchesGuard] = instance->getBatches();

		// Critical section #2: Batch map
		BatchData<BatchParams>* batch = nullptr;
		if (std::scoped_lock mapLock(instance->batchesGuard); true)
			batch = &instance->batches[batchParams];

		// Critical section #3: Batch
		std::scoped_lock batchLock(batch->guard);

		// Insert vertex data
		vertexBufferSize = newGeometry.vertices.size();
		vertexBufferOffset = fitIntoBuffer(std::move(newGeometry.vertices), &batch->vertexHoles, &batch->geometry->vertices);

		// Update index array
		for (auto&& idx : newGeometry.indices)
			idx += vertexBufferOffset;

		// Insert index data
		indexBufferSize = newGeometry.indices.size();
		indexBufferOffset = fitIntoBuffer(std::move(newGeometry.indices), &batch->indexHoles, &batch->geometry->indices);

		// Query vbo update
		batch->bNeedsVBOUpdate = true;

		// Reset bounding box
		cachedBoundingBox = {};
		batch->cachedBoundingBox = {};
	}

	const BatchParams batchParams;
	const QSharedPointer<RenderGeometryData<typename BatchParams::VertexType>> mainBuffer;

private:
	mutable std::mutex boxGuard;
	mutable BoundingBox cachedBoundingBox;
};

template<typename BatchParams>
struct BatchData
{
	using VertexType = BatchParams::VertexType;

	BatchData() : geometry(QSharedPointer<RenderGeometryData<VertexType>>::create()) {};
	BatchData& operator=(BatchData&& other)
	{
		geometry = std::move(other.geometry);
		sections = std::move(other.sections);
		vertexHoles = std::move(other.vertexHoles);
		indexHoles = std::move(other.indexHoles);
		bNeedsVBOUpdate = other.bNeedsVBOUpdate;
		bNeedsHolesUpdate = other.bNeedsHolesUpdate;
		return *this;
	}

	QSharedPointer<RenderGeometryData<VertexType>> geometry;
	std::unordered_map<qint64 /*guid*/, QSharedPointer<BatchedSection<BatchParams>>> sections;

	std::list<BufferHole> vertexHoles;
	std::list<BufferHole> indexHoles;

	bool bNeedsVBOUpdate = false;
	bool bNeedsHolesUpdate = false;
	mutable std::mutex guard;

    void defragmentHoles()
    {
        defragmentHoleList(&vertexHoles);
        defragmentHoleList(&indexHoles);
    }

	// Rendering
	mutable bool bIsCulled = false;
	mutable BoundingBox cachedBoundingBox;
	mutable std::mutex boxGuard;
    const auto& getBoundingBox() const
    {
        if (std::scoped_lock lock(boxGuard); cachedBoundingBox.sizes.isNull())
        {
            if constexpr (std::is_same_v<VertexType, QVector3D>)
            {
                cachedBoundingBox = BoundingBox::fromPoints(geometry->vertices);
            }
            else
            {
                cachedBoundingBox = BoundingBox::fromPoints(geometry->vertices, [](auto&& p) { return p.position; });
            }
        }

        return cachedBoundingBox;
    }

private:
	void defragmentHoleList(std::list<BufferHole>* holeList)
	{
		if (holeList->size() < 2)
			return;

		holeList->sort();
		for (auto vit = holeList->begin(); vit != --holeList->end();)
		{
			auto next = std::next(vit);
			if (vit->offset + vit->size == next->offset)
			{
				vit->size += next->size;
				holeList->erase(next);
			}
			else
			{
				++vit;
			}
		}
	}
};

template<typename BatchParams>
class DBatchingMarker : public DMarker
{
	using VertexType = BatchParams::VertexType;
	using Painter = BatchParams::PainterType;

public:
	DBatchingMarker() = default;

	// Drawable
	virtual void initialize() override
	{
		createShader();
		geometry[ELOD::Last] = QSharedPointer<RenderGeometryData<VertexType>>::create();
	}

	virtual void cacheBoundingBox() override
	{
		// Always render
		cachedBoundingBox.sizes = { getMaxGridCoord(), getMaxGridCoord(), getMaxGridCoord() };
	}

	virtual void updateCullStatus(const OmnigenCamera& camera, int vIdx) override 
	{ 
		bIsCulled = !shouldDraw(vIdx);
		if (bIsCulled)
			return;

        // Critical section #2: Batch map
        std::scoped_lock mapLock(batchesGuard);
        for (auto&& [batchParams, batch] : batches)
        {
            // Critical section #3: Batch
            std::scoped_lock batchLock(batch.guard);
			batch.bIsCulled = !camera.isBoxInFrustum(batch.getBoundingBox());
        }
	};

	virtual quint32 getShaderLabel() const override { return painter.getShaderLabel(); };
	virtual void bindShader(const OmnigenCamera& camera) override { return painter.bindShader(camera); }
	virtual void draw() override
	{
		// Critical section #2: Batch map
		std::scoped_lock mapLock(batchesGuard);
		for (auto&& [batchParams, batch] : batches)
		{
			// Critical section #3: Batch
			std::scoped_lock batchLock(batch.guard);
			if (batch.bIsCulled)
				continue;

			if (batch.bNeedsVBOUpdate)
			{
				batch.geometry->fillVbo();
				batch.bNeedsVBOUpdate = false;
			}

			if (batch.bNeedsHolesUpdate)
			{
				batch.defragmentHoles();
				batch.bNeedsHolesUpdate = false;
			}

			painter.draw(*batch.geometry, batchParams);
		}
	}
	virtual void unbindShader() override { return painter.unbindShader(); };
	virtual void createShader() override { return painter.createShader(); }

    virtual bool shouldDraw(int vIdx) const override 
	{
		return bShouldDraw<std::decay_t<decltype(*this)>>[vIdx] && painter.shouldDraw();
    }

	virtual ShaderPipeline& getShaderPipeline() const override { return painter.shaderPipeline; };

	struct BatchedMarkerPoint
	{
		BatchedSection<BatchParams>* section;
		IndexType idx;
	};

	void resetQuadTree() const
	{
		qTree = nullptr;
	}

	const auto& getQuadTree() const
	{
		std::scoped_lock lock(qTreeGuard);
		if (!qTree) [[unlikely]]
		{
			// 3 squares lookup margin
			constexpr float minCoord = -3 * GRID_SEGMENT_WIDTH;
			constexpr float maxCoord = (GRID_SEGMENT_COUNT + 3) * GRID_SEGMENT_WIDTH;
			qTree = new tml::qtree<float, BatchedMarkerPoint>(minCoord, maxCoord, maxCoord, minCoord);

			std::scoped_lock mapLock(batchesGuard);
			for (auto&& [params, batch] : batches)
			{
				auto&& vertices = batch.geometry->vertices;

				std::scoped_lock batchLock(batch.guard);
				for (auto&& [offset, section] : batch.sections)
				{
					IndexType vertexEnd = section->getVertexBufferOffset() + section->getVertexBufferSize();
					for (IndexType i = section->getVertexBufferOffset(); i < vertexEnd; ++i)
					{
						if constexpr (std::is_same_v<VertexType, QVector3D>)
						{
							auto&& p = vertices[i];
							qTree->add_node(p.x(), p.z(), BatchedMarkerPoint{ section.get(), i - section->getVertexBufferOffset() });
						}
						else
						{
							auto&& p = vertices[i].position;
							qTree->add_node(p.x(), p.z(), BatchedMarkerPoint{ section.get(), i - section->getVertexBufferOffset() });
						}
					}
				}
			}
		}

		return *qTree;
	}

	QSharedPointer<BatchedSection<BatchParams>> findSectionByGuid(qint64 guid) const
	{
		for (auto&& [params, batch] : batches)
			if (auto sit = batch.sections.find(guid); sit != batch.sections.end())
				return sit->second;

		Q_ASSERT(false);
		return {};
	}

	bool hasSections() const
	{
		for (auto&& [params, batch] : batches)
			if (!batch.sections.empty())
				return true;

		return false;
	}

	auto getBatches() const { return std::tie(batches, batchesGuard); };
	mutable Painter painter;

protected:
	std::map<BatchParams, BatchData<BatchParams>> batches;
	mutable std::mutex batchesGuard;

	mutable std::mutex qTreeGuard;
	mutable tml::qtree<float, BatchedMarkerPoint>* qTree = nullptr;

	template<typename VertexType, typename BatchParams>
	friend QSharedPointer<BatchedSection<BatchParams>> spawnBatched(GeometryData<VertexType>&&, BatchParams, std::optional<qint64>);

	template<typename BatchParams>
	friend void despawnBatched(const QSharedPointer<BatchedSection<BatchParams>>&);

    template<typename BatchParams>
    friend void updateBatch(const BatchParams&);

	template<typename BatchParams>
	friend void clearAllBatches();

	friend BatchedSection;

	FRIEND_OMNIBIN_T(DBatchingMarker);
};

template<typename BatchParams>
QSharedPointer<DBatchingMarker<BatchParams>> gBatchingMarkerInstance;

template<typename BatchParams>
std::mutex gBatchingMarkerInstanceGuard;

inline void omniSave(const BatchedSectionBase& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.vertexBufferOffset;
	omniBin << object.vertexBufferSize;
	omniBin << object.indexBufferOffset;
	omniBin << object.indexBufferSize;
}

inline void omniLoad(BatchedSectionBase& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> const_cast<qint64&>(object.guid);
	omniBin >> object.vertexBufferOffset;
	omniBin >> object.vertexBufferSize;
	omniBin >> object.indexBufferOffset;
	omniBin >> object.indexBufferSize;
}

template<typename BatchParams, bool ForInheritanceOnly = false>
inline void omniSave(const BatchedSection<BatchParams, ForInheritanceOnly>& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << static_cast<const BatchedSectionBase&>(object);
	omniBin << object.batchParams;
}

template<typename BatchParams, bool ForInheritanceOnly = false>
inline void omniLoad(BatchedSection<BatchParams, ForInheritanceOnly>& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> static_cast<BatchedSectionBase&>(object);
	omniBin >> const_cast<BatchParams&>(object.batchParams);
}

template<typename BatchParams>
inline void omniSave(const BatchData<BatchParams>& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.geometry;
	omniBin << object.sections;
	omniBin << object.vertexHoles;
	omniBin << object.indexHoles;
}

template<typename BatchParams>
inline void omniLoad(BatchData<BatchParams>& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.geometry;
	omniBin >> object.sections;
	omniBin >> object.vertexHoles;
	omniBin >> object.indexHoles;

	object.bNeedsVBOUpdate = true;
	for (auto&& [guid, section] : object.sections)
		const_cast<decltype(object.geometry)&>(section->mainBuffer) = object.geometry;
}

template<typename BatchParams>
inline void omniSave(const DBatchingMarker<BatchParams>& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.guid;
	omniBin << object.batches;
}

template<typename BatchParams>
inline void omniLoad(DBatchingMarker<BatchParams>& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.guid;
	omniBin >> object.batches;
}