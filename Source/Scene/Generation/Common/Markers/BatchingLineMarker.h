#pragma once

#pragma once
#include "Utils/CoreUtils.h"
#include "Scene/OmnigenDrawable.h"
#include "Scene/Generation/Common/Markers/BatchMarker.h"
#include "Scene/Generation/Common/Markers/LineMarkerData.h"

struct LinePainter
{
	ShaderPipeline shaderPipeline;

	quint32 getShaderLabel() const { return typeid(LinePainter).hash_code(); };
	void createShader();
	void bindShader(const OmnigenCamera& camera);
	void draw(RenderGeometryData<>& geometry, const struct LineBatchParams&);
	void unbindShader();

	virtual bool shouldDraw() const { return true; };
};

struct LineBatchParams
{
	using VertexType = QVector3D;
	using PainterType = LinePainter;

	LineBatchParams(QVector4D inColor, float inHeight = 0.0f)
		: color(std::move(inColor))
		, height(inHeight)
	{}

	QVector4D color;
	float height;

	// Batch all into 1
	bool operator<(const LineBatchParams& other) const;
};
using DBatchingLineMarker = DBatchingMarker<LineBatchParams>;

GeometryData<> buildLineGeometry(std::vector<QVector3D> controlPoints, bool isLoop = false);
QSharedPointer<BatchedSection<LineBatchParams>> spawnBatchedLine(const std::vector<QVector3D>& inControlPoints, const QVector4D& inColor = QVector4D(1, 1, 1, 1), bool inIsLoop = false, float inHeight = 0.0f);
QSharedPointer<BatchedSection<LineBatchParams>> spawnBatchedLine(const std::vector<GVector2D>& inControlPoints, const QVector4D& inColor = QVector4D(1, 1, 1, 1), bool inIsLoop = false, float inHeight = 0.0f);
QSharedPointer<BatchedSection<LineBatchParams>> spawnBatchedLine(const QVector3D& inControlPoint, const float inHeight = 10000, const QVector4D& inColor = QVector4D(1, 1, 1, 1));
QSharedPointer<BatchedSection<LineBatchParams>> spawnBatchedLine(const QVector3D& p1, const QVector3D& p2, const QVector4D& inColor = QVector4D(1, 1, 1, 1), float inHeight = 0.0f, ELineDecorator ld = ELineDecorator::Arc);

