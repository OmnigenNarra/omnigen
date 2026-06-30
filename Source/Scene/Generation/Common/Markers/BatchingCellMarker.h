#pragma once
#include "Utils/CoreUtils.h"
#include "Scene/OmnigenDrawable.h"
#include "Scene/Generation/Common/Markers/BatchMarker.h"

struct CellVertex
{
	QVector3D position;
	QVector3D normal;
	int cellId;

	// For triangulation
	operator GVector2D() const noexcept { return position; };
};

enum class FCellStates
{
    None = 0,
    Hovered = 1 << 0,
    Selected = 1 << 1,
};
DECLARE_FLAG_OPERATORS(FCellStates);

struct CellPainter
{
	ShaderPipeline shaderPipeline;
	GLuint cellDataBufferId;
	std::vector<QVector4D> cellColors;
	mutable bool bNeedsBufferUpdate = true;

	std::vector<int> trianglesToCells;
	std::vector<FCellStates> cellStates;

	quint32 getShaderLabel() const { return typeid(CellPainter).hash_code(); };
	void createShader();
	void bindShader(const OmnigenCamera& camera);
	void draw(RenderGeometryData<CellVertex>& geometry, const struct CellBatchParams&);
	void unbindShader();

	virtual bool shouldDraw() const { return true; };
};

struct CellBatchParams
{
	using VertexType = CellVertex;
	using PainterType = CellPainter;

	// Batch all into 1
	bool operator<(const CellBatchParams&) const { return false; };
};
using DCellMarker = DBatchingMarker<CellBatchParams>;

template<>
struct BatchedSection<CellBatchParams> : BatchedSection<CellBatchParams, true>
{
	using BatchedSection<CellBatchParams, true>::BatchedSection;
	int cellIdx;
};