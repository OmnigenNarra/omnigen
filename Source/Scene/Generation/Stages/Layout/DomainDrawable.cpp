#include "stdafx.h"
#include "DomainDrawable.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Editor/StageTools/Layout/LayoutSelection.h"
#include "Utils/CoreUtils.h"
#include "Omnigen.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "DomainHandleDrawable.h"
#include "Scene/Generation/Stages/Layout/DomainSquareDrawable.h" 
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"

const QMap<EDomainType, QVector4D> DDomain::Colors = {
    { EDomainType::Terrain, QVector4D(0.6784, 0.5922, 0.4784, 1.0) },
    { EDomainType::Biome, QVector4D(0.1961, 0.3294, 0.2196, 1.0) },
    { EDomainType::Water, QVector4D(0.3785, 0.5059, 0.6549, 1.0) },
    { EDomainType::Last, QVector4D(0.75, 0.75, 0.75, 1.0) }
};

void DDomain::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();

    // Load transformation matrices.
    shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
}

void DDomain::cacheBoundingBox()
{
    auto&& [x1, z1] = *squares.cbegin();
    QVector3D nbl = { x1 * GRID_SEGMENT_WIDTH, 0, z1 * GRID_SEGMENT_WIDTH };

    auto&& [x2, z2] = *squares.crbegin();
    QVector3D ftr = { (x2 + 1) * GRID_SEGMENT_WIDTH, 10, (z2 + 1) * GRID_SEGMENT_WIDTH };

    cachedBoundingBox = { nbl, ftr - nbl };
}

void DDomain::draw()
{
    auto handle = getHandle().lock();
    bool bHovered = Design::DomainSelection::isDomainHovered(handle);
    bool bSelected = handle->isSelected();

    auto& vbo = getActiveGeometry()->vbo;
    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    vbo.release();

    // Draw the terrain mesh.
    ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, Colors[type]);
    ShaderPipeline::current->setUniformValue(EShaderUniform::Height, type == EDomainType::Terrain ? getData<EDomainType::Terrain>()->maxHeight : (GRID_SEGMENT_WIDTH * 0.5f));

    if (handle->isSelected())
    {
        auto& indices = getActiveGeometry()->indices;
        ShaderPipeline::current->setUniformValue(EShaderUniform::DrawType, 0);
        glDrawElements(GL_QUADS, indices.size(), GL_UNSIGNED_INT, indices.data());
    }

    ShaderPipeline::current->setUniformValue(EShaderUniform::DrawType, 1);
    if (bSelected || bHovered)
        glDrawElements(GL_LINES, wireframeIndices.size(), GL_UNSIGNED_INT, wireframeIndices.data());
    else
        glDrawElements(GL_LINES, boundsIndices.size(), GL_UNSIGNED_INT, boundsIndices.data());
}

void DDomain::unbindShader()
{
    shaderPipeline.release();
}

bool DDomain::shouldDraw(int vIdx) const
{
    return Generation::Data::get()->getGenerationStage() == EGenerationStage::Layout;
}

BoundingBox DDomain::getBoundingRectangle() const
{
    // Calculate the rectangle of the domain perimeter
    float xMin = std::numeric_limits<float>::max();
    float xMax = -1.f;
    float zMin = std::numeric_limits<float>::max();
    float zMax = -1.f;

    for (auto&& [A, B] : perimeter)
    {
        xMin = std::fmin(xMin, std::fmin(A.x, B.x));
        xMax = std::fmax(xMax, std::fmax(A.x, B.x));
        zMin = std::fmin(zMin, std::fmin(A.z, B.z));
        zMax = std::fmax(zMax, std::fmax(A.z, B.z));
    }

    float boundLeft = xMin;
    float boundRight = xMax;
    float boundBottom = zMin;
    float boundTop = zMax;

    return BoundingBox( GVector2D{boundLeft, boundBottom}, GVector2D{boundRight - boundLeft, boundTop - boundBottom} );
}

bool DDomain::isPointInDomain(const GVector2D& inPoint) const
{
    auto&& domainBB = getBoundingRectangle();
    if (!domainBB.contains(inPoint))
        return false;

    const float maxRayLength = std::sqrt((domainBB.sizes.x() * domainBB.sizes.x()) + (domainBB.sizes.z() * domainBB.sizes.z()));

    bool inside = false;
    for (auto&& seg : perimeter)
    {
        if (seg.intersects({ inPoint, inPoint }, true))
            return true;

        if (seg.intersects({ inPoint, { inPoint.x, inPoint.z * maxRayLength} }, true))
        {
            inside = !inside;
        }
    }

    return inside;
}

void DDomain::setType(EDomainType newType)
{
    if (type == newType)
        return;

    type = newType;
    createData();
}

void DDomain::setName(const QString& newName)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    data->name = newName;
    emit Editable::modified(sharedFromThis());
}

void DDomain::setData(QSharedPointer<DomainDataBase> newData)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    data = newData;
    bool isSelected = handle.lock()->isSelected();

    if (isSelected)
        Omnigen::get()->getProperties()->clear();
        
    emit Editable::modified(sharedFromThis());
}

void DDomain::setSquares(const QSet<GPoint>& newSquares)
{
    emit Editable::aboutToBeModified(sharedFromThis());
    Generation::Data::get()->updateDomainSquaresMap(sharedFromThis(), squares, newSquares);

    squares = newSquares;
    update();

    emit Editable::modified(sharedFromThis());
}

void DDomain::bindHandle(QSharedPointer<DDomainHandle> dh)
{
    handle = dh;
    dh->ownedDomain = sharedFromThis();
    dh->update();
}

void DDomain::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    // Compute final vertex position.
    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform int drawType;\n"
        "uniform mediump mat4 viewProjection;\n\n"
        "uniform float height;\n"

        "in vec4 vertex;\n\n"

        "void main(void)\n"
        "{\n"
        "    vec4 vtx = vertex;\n"
        "    if (vtx.y != 0) vtx.y = height;\n"
        "    if (drawType == 1) vtx.y += 10.0;\n"
        "    gl_Position = viewProjection * vtx;\n"
        "}\n";

    // The final color depends on the height. Simple, but brings a lot.
    const char* fragmentShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform int drawType;\n"
        "uniform vec4 domainColor;\n\n"
        "uniform int objectID;\n\n"

        "vec4 domainWireframeColor = domainColor * 1.33;"
        "layout (location = 0) out vec4 fragColor;\n"
        "layout (location = 1) out vec4 outData;\n\n"

        "void main(void)\n"
        "{\n"
        "	fragColor = (drawType == 0) ? domainColor : domainWireframeColor;\n"
        "   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"
        "};\n";

    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    bool ok = shaderPipeline.link();

    // Set shader parameters' locations.
    shaderPipeline.set(EShaderAttribute::Position, "vertex");

    shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
    shaderPipeline.set(EShaderUniform::DrawType, "drawType");
    shaderPipeline.set(EShaderUniform::Color0, "domainColor");
    shaderPipeline.set(EShaderUniform::Height, "height");

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}

void DDomain::createDefaultLodLevel()
{
    assignLodLevel(ELOD::Last, QSharedPointer<RenderGeometryData<>>::create());
}

void DDomain::createData()
{
    data = EDomainTypeConstexpr::UseIn<EAC::CreateDomainData>(type);
}

void DDomain::update()
{
    auto& vertices = getActiveGeometry()->vertices;
    auto& indices = getActiveGeometry()->indices;

    // Clear
    vertices.clear();
    indices.clear();
    wireframeIndices.clear();
    boundsIndices.clear();
    perimeter.clear();

    std::tie(vertices, indices, wireframeIndices, boundsIndices, perimeter) = computePerimeterForSquares(squares);

    updateVbo(activeLOD);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////