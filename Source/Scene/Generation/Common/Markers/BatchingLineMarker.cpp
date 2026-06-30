#include "stdafx.h"
#include "BatchingLineMarker.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"
#include "Scene/Generation/OmnigenGenerationData.h"

void LinePainter::createShader()
{
	if (shaderPipeline.isLinked())
		return;

	// Compute final vertex position.
	const char* vertexShaderSource =
		"#version " OPENGL_SHADER_VER "\n"
		"uniform mat4 viewProjection;\n"
		"uniform float height;\n\n"

		"in vec4 position;\n\n"

		"void main(void)\n"
		"{\n"
		"    vec4 pos = position;\n"
		"    pos.y += height;\n"
		"    gl_Position = viewProjection * pos;\n"
		"}\n";

	// The final color depends on the height. Simple, but brings a lot.
	const char* fragmentShaderSource =
		"#version " OPENGL_SHADER_VER "\n"
		"uniform vec4 color;\n"
		"uniform int objectID;\n\n"

        "layout (location = 0) out vec4 fragColor;\n"
        "layout (location = 1) out vec4 outData;\n\n"

		"void main(void)\n"
		"{\n"
		"   fragColor = color;\n"
		"   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"
		"};\n";

	shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
	shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
	bool ok = shaderPipeline.link();

	shaderPipeline.set(EShaderAttribute::Position, "position");
	shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
	shaderPipeline.set(EShaderUniform::Color0, "color");
	shaderPipeline.set(EShaderUniform::Height, "height");

	shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}

void LinePainter::bindShader(const OmnigenCamera& camera)
{
	shaderPipeline.bind();
	shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
}

void LinePainter::unbindShader()
{
	shaderPipeline.release();
}

void LinePainter::draw(RenderGeometryData<>& geometry, const LineBatchParams& params)
{
	ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, params.color);
	ShaderPipeline::current->setUniformValue(EShaderUniform::Height, params.height);

	auto& vbo = geometry.vbo;
	vbo.bind();
	ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
	vbo.release();

	auto& indices = geometry.indices;
	glDrawElements(GL_LINES, indices.size(), GL_UNSIGNED_INT, indices.data());
}

bool LineBatchParams::operator<(const LineBatchParams& other) const
{
	return std::tuple(color.x(), color.y(), color.z(), color.w(), height) < std::tuple(other.color.x(), other.color.y(), other.color.z(), other.color.w(), other.height);
}

GeometryData<> buildLineGeometry(std::vector<QVector3D> controlPoints, bool isLoop /*=false*/)
{
	GeometryData<> geometry;
	auto& vertices = geometry.vertices;
	auto& indices = geometry.indices;

	vertices = std::move(controlPoints);
	std::vector<IndexType> controlPointsIndices(vertices.size());
	for (int i = 0; i < vertices.size(); ++i)
		controlPointsIndices[i] = i;

	appendLines(indices, controlPointsIndices, isLoop);

	return geometry;
}

QSharedPointer<BatchedSection<LineBatchParams>> spawnBatchedLine(const std::vector<QVector3D>& inControlPoints, const QVector4D& inColor /*= QVector4D(1, 1, 1, 1)*/, bool inIsLoop /*= false*/, float inHeight /*= 0.0f*/)
{
	return spawnBatched(buildLineGeometry(inControlPoints), LineBatchParams{ inColor, inHeight });
}

QSharedPointer<BatchedSection<LineBatchParams>> spawnBatchedLine(const std::vector<GVector2D>& inControlPoints, const QVector4D& inColor /*= QVector4D(1, 1, 1, 1)*/, bool inIsLoop /*= false*/, float inHeight /*= 0.0f*/)
{
	return spawnBatched(buildLineGeometry({ inControlPoints.begin(), inControlPoints.end() }), LineBatchParams{ inColor, inHeight });
}

QSharedPointer<BatchedSection<LineBatchParams>> spawnBatchedLine(const QVector3D& inControlPoint, const float inHeight /*= 10000*/, const QVector4D& inColor /*= QVector4D(1, 1, 1, 1)*/)
{
	return spawnBatched(buildLineGeometry(std::vector{ inControlPoint, inControlPoint + QVector3D(0, inHeight, 0) }), LineBatchParams{ inColor, 0.0f });
}

QSharedPointer<BatchedSection<LineBatchParams>> spawnBatchedLine(const QVector3D& p1, const QVector3D& p2, const QVector4D& inColor /*= QVector4D(1, 1, 1, 1)*/, float inHeight /*= 0.0f*/, ELineDecorator ld /*= ELineDecorator::Arc*/)
{
	switch (ld)
	{
	case ELineDecorator::Arc:
		return spawnBatched(buildLineGeometry(std::vector{ p1, (p1 + p2) * 0.5 + QVector3D(0, distance(p1, p2), 0), p2 }), LineBatchParams{ inColor, inHeight });

	case ELineDecorator::Arrow:
		float w = distance(p1, p2) * 0.2;
		auto r = p2 + QQuaternion::fromEulerAngles(0, 45, 0).rotatedVector(p1 - p2).normalized() * w;
		auto l = p2 + QQuaternion::fromEulerAngles(0, -45, 0).rotatedVector(p1 - p2).normalized() * w;
		return spawnBatched(buildLineGeometry(std::vector{ r, p2, p1, p2, l }), LineBatchParams{ inColor, inHeight });
	}

	Q_ASSERT(false);
	return {};
}
