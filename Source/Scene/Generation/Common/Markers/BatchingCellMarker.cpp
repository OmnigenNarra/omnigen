#include "stdafx.h"
#include "BatchingCellMarker.h"
#include "Editor/Sections/Viewport/OmnigenCameraManager.h"

void CellPainter::createShader()
{
    if (shaderPipeline.isLinked())
        return;

	// Compute final vertex position.
	const char* vertexShaderSource =
		"#version " OPENGL_SHADER_VER "\n"
		"uniform mat4 viewProjection;\n\n"

		"in vec3 position;\n"
		"in vec3 normal;\n"
		"in int cellId;\n"

		"out vec3 pNormal;\n"
		"flat out int pCellId;\n\n"

		"void main(void)\n"
		"{\n"
		"    gl_Position = viewProjection * vec4(position, 1);\n"
		"    pCellId = cellId;\n"
		"    pNormal = normal;\n"
		"}\n";

	// The final color depends on the height. Simple, but brings a lot.
	const char* fragmentShaderSource =
		"#version " OPENGL_SHADER_VER "\n"
		"uniform int objectID;\n\n"

        "const vec3 lightDirection = vec3(0.57735026919f, -0.57735026919f, 0.57735026919f);\n"
        "const float ambient = 0.2f;\n\n"

		"layout(std430, binding = 0) buffer cellData\n"
		"{\n"
		"    vec4 cellColors[];\n"
		"};\n\n"

		"flat in int pCellId;\n"
		"in vec3 pNormal;\n"
		"layout (location = 0) out vec4 fragColor;\n"
		"layout (location = 1) out vec4 outData;\n\n"

		"void main(void)\n"
		"{\n"
		"   fragColor = cellColors[pCellId];\n"
		"   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"

        "   float factor = dot(lightDirection, -pNormal);\n"
        "   factor = clamp(factor, 0.0f, 1.0f - ambient) + ambient;\n"
        "   fragColor = fragColor * factor;\n"
        "   fragColor.w = 1;\n"
		"}";

	shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
	shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
	bool ok = shaderPipeline.link();

	shaderPipeline.set(EShaderAttribute::Position, "position");
	shaderPipeline.set(EShaderAttribute::Normal, "normal");
	shaderPipeline.set(EShaderAttribute::UV, "cellId");

	shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
	shaderPipeline.set(EShaderUniform::Color0, "cellData"); // Upload global array of cell colors here

	shaderPipeline.set(EShaderUniform::ObjectID, "objectID");

	// Init cell data
	auto* gl = QOpenGLContext::currentContext()->extraFunctions();
	gl->glGenBuffers(1, &cellDataBufferId);
}

void CellPainter::bindShader(const OmnigenCamera& camera)
{
	shaderPipeline.bind();
	shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
}

void CellPainter::unbindShader()
{
	shaderPipeline.release();
}

void CellPainter::draw(RenderGeometryData<CellVertex>& geometry, const CellBatchParams&)
{
	auto* gl = QOpenGLContext::currentContext()->extraFunctions();
	if (bNeedsBufferUpdate)
	{
		auto cellColorsSizeInBytes = cellColors.size() * sizeof(decltype(cellColors)::value_type);
		void* cellColorsDataPtr = cellColors.data();

		gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, cellDataBufferId);
		gl->glBufferData(GL_SHADER_STORAGE_BUFFER, cellColorsSizeInBytes, cellColorsDataPtr, GL_DYNAMIC_DRAW);
		gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		bNeedsBufferUpdate = false;
	}

	gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cellDataBufferId);

	auto& vbo = geometry.vbo;
	vbo.bind();
	ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, offsetof(CellVertex, position), 3, geometry.size());
	ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Normal, GL_FLOAT, offsetof(CellVertex, normal), 3, geometry.size());
	ShaderPipeline::current->setAttributeBufferI(EShaderAttribute::UV, GL_INT, offsetof(CellVertex, cellId), 1, geometry.size());
	vbo.release();

	auto& indices = geometry.indices;
	glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, indices.data());
}