#pragma once
#include "Scene/Generation/Common/Markers/BatchingLineMarker.h"
#include "IHSrcInfo.h"
#include "IsohypseData.h"
#include "Utils/CircularVectorView.h"

struct IHProtoData;

struct IsohypseBatchParams : LineBatchParams
{
	using VertexType = QVector3D;
	using PainterType = LinePainter;

	IsohypseBatchParams() : LineBatchParams({ 0.9, 0.45, 0, 1 }, 0.0f) {};

	// Batch all into 1
	bool operator<(const IsohypseBatchParams&) const { return false; };
};
using DBatchingIsohypseMarker = DBatchingMarker<IsohypseBatchParams>;

template<>
struct BatchedSection<IsohypseBatchParams, false> : BatchedSection<IsohypseBatchParams, true>, public QEnableSharedFromThis<Isohypse>
{
	using BatchedSection<IsohypseBatchParams, true>::BatchedSection;

	const auto getCircularPoints() const { return CircularVectorView<std::vector, QVector3D>(mainBuffer->vertices, vertexBufferOffset, vertexBufferSize); }
	const auto getPoints() const { return std::vector<QVector3D>(mainBuffer->vertices.begin() + vertexBufferOffset, mainBuffer->vertices.begin() + vertexBufferOffset + vertexBufferSize); }
	const auto& getLevel() const { return level; }
	const auto& getHeight() const { return data.height; }
	const auto& getSources() const { return data.sources; }
	const auto& getDescendants() const { return descendants; }
	const auto& getPreflow() const { return preflow; }
	const auto& getParentIHs() const { return parentIhs; }
	const auto& getParentGuids() const { return parentGuids; }

	const QVector3D& getSourcePoint(int i) const;
	IHSrcInfo getNearestDescendant(int i, int dir = 0, const QVector3D& ignoredPoint = QVector3D(-1, -1, -1));

	void setPoints(const std::vector<QVector3D>& newPoints);
	void setDescendant(int i, const IHSrcInfo& descendant);
	void addPreflow(int i, const IHSrcInfo& preflowPoint);
	void addParent(BatchedSection<IsohypseBatchParams, false>* parent);

	float getLength() const;

	/// <summary>
	/// ///////////////////////////////////////////////////////////////////////////////////////
	/// </summary>
	int level = -1;

	// IH points produced by this IH's points
	std::vector<IHSrcInfo> descendants;

	// IH points for which this IH's points are nearest descendants
	std::vector<std::vector<IHSrcInfoMulti>> preflow;

	// IHs considered as parents
	std::set<BatchedSection*> parentIhs;
	mutable std::set<qint64> parentGuids; // for serialization

	// Data from whcih IH was created
	IHProtoData data;

	mutable float lazyLength = -1.f;
	mutable std::mutex lengthGuard;
};
using Isohypse = BatchedSection<IsohypseBatchParams>;
using IsohypseBase = BatchedSection<IsohypseBatchParams, true>;

QSharedPointer<Isohypse> spawnBatchedIH(const struct IHProtoData& data, int inLevel);

inline void omniSave(const Isohypse& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << static_cast<const IsohypseBase&>(object);
	omniBin << object.level;

	omniBin << object.descendants;
	omniBin << object.preflow;

	for (auto* parent : object.parentIhs)
		object.parentGuids.insert(parent->getGuid());

	omniBin << object.parentGuids;
	omniBin << object.data;
}

inline void omniLoad(Isohypse& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> static_cast<IsohypseBase&>(object);

	omniBin >> const_cast<int&>(object.level);
	omniBin >> object.descendants;
	omniBin >> object.preflow;

	omniBin >> object.parentGuids;
	omniBin >> object.data;
}