#include "stdafx.h"
#include "ManipulationGizmo.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"

const QMap<EArrowType, QVector4D> DManipulationGizmo::arrowColors = {
    {EArrowType::YArrow, QVector4D(0, 0, 1, 1)},
    {EArrowType::XArrow, QVector4D(1, 0, 0, 1)},
    {EArrowType::ZArrow, QVector4D(0, 1, 0 ,1)}
};

void DManipulationGizmo::draw()
{
    ShaderPipeline::current->setUniformValue(EShaderUniform::Height, gizmoOffset);

    // Y Arrow
    if(useArrow[EArrowType::YArrow])
    {
        auto& vbo = getActiveGeometry()->vbo;
        vbo.bind();
        ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
        vbo.release();

        ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, arrowColors[EArrowType::YArrow]);

        auto& indices = getActiveGeometry()->indices;
        glDrawElements(GL_LINES, indices.size(), GL_UNSIGNED_INT, indices.data());
    }

    // X Arrow 
    if(useArrow[EArrowType::XArrow])
    {
        xArrow->vbo.bind();
        ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
        xArrow->vbo.release();

        ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, arrowColors[EArrowType::XArrow]);

        glDrawElements(GL_LINES, xArrow->indices.size(), GL_UNSIGNED_INT, xArrow->indices.data());
    }

    // Z Arrow
    if(useArrow[EArrowType::ZArrow])
    {
        zArrow->vbo.bind();
        ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
        zArrow->vbo.release();

        ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, arrowColors[EArrowType::ZArrow]);

        glDrawElements(GL_LINES, zArrow->indices.size(), GL_UNSIGNED_INT, zArrow->indices.data());
    }
}

void DManipulationGizmo::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();
    shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
    glGetFloatv(GL_LINE_WIDTH, &lastLineWidth);
    glLineWidth(5);
}

void DManipulationGizmo::unbindShader()
{
    shaderPipeline.release();
    glLineWidth(lastLineWidth);
}

void DManipulationGizmo::cacheBoundingBox()
{
    for (auto&& arrow : arrowPoints)
        cachedBoundingBox = BoundingBox::merge(cachedBoundingBox, BoundingBox::fromPoints(arrow));
}

void DManipulationGizmo::updateArrowPoints()
{
    auto& yVertices = getActiveGeometry()->vertices;
    auto& xVertices = xArrow->vertices;
    auto& zVertices = zArrow->vertices;
    arrowPoints.clear();

    // Move All verts of gizmo
    for (int i = 0; i < yVertices.size(); i++)
    {
        yVertices[i] = yVertices[i] + gizmoOffset;
        xVertices[i] = xVertices[i] + gizmoOffset;
        zVertices[i] = zVertices[i] + gizmoOffset;
    }

    updateVbo(activeLOD);
    xArrow->fillVbo();
    zArrow->fillVbo();

    // Save arrow points
    arrowPoints.insert(EArrowType::XArrow, xVertices);
    arrowPoints.insert(EArrowType::YArrow, yVertices);
    arrowPoints.insert(EArrowType::ZArrow, zVertices);

    gizmoBasePos = gizmoBasePos + gizmoOffset;
    gizmoOffset = QVector3D(0, 0, 0);
}

std::optional<EArrowType> DManipulationGizmo::isMouseOverGizmo(int mousePosX, int mousePosY)
{
    auto* camera = OmnigenCameraMgr::get()->getCameraForActiveViewport();
    auto&& cameraPos = camera->getPosition();
    auto rayVec = camera->makeRayFromCursor(mousePosX, mousePosY);

    std::array<QVector3D, 2> ray({ cameraPos, (cameraPos + rayVec * 500000) });

    for (auto&& it = arrowPoints.keyValueBegin(); it != arrowPoints.keyValueEnd(); ++it)
    {
        if (!useArrow[(*it).first])
            continue;

        float dist = std::get<2>(distance(ray, std::array<QVector3D, 2>({ (*it).second[0], (*it).second[1] })));
        if (dist < 1000.0)
            return (*it).first;
    }

    return {};
}

void DManipulationGizmo::grabGizmoAxis(int mousePosX, int mousePosY, EArrowType axis)
{
    arrowSelected = axis;
    mouseGripOffset = getMovementDelta(mousePosX, mousePosY);
}

void DManipulationGizmo::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    // Compute final vertex position.
    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mediump mat4 viewProjection;\n\n"
        "uniform vec3 offset;\n"

        "in vec4 vertex;\n\n"

        "void main(void)\n"
        "{\n"
        "    vec4 pos = vertex;\n"
        "    pos += vec4(offset, 0.0);\n"
        "    gl_Position = viewProjection * pos;\n"
        "}\n";

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

    shaderPipeline.set(EShaderAttribute::Position, "vertex");
    shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
    shaderPipeline.set(EShaderUniform::Color0, "color");
    shaderPipeline.set(EShaderUniform::Height, "offset");

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}

void DManipulationGizmo::createDefaultLodLevel()
{
    auto data = QSharedPointer<RenderGeometryData<>>::create();

    auto& vertices = data->vertices;
    auto& indices = data->indices;

    gizmoBasePos = QVector3D(0, 0, 0);

    // Y arrow
    vertices = {QVector3D(gizmoBasePos.x(), gizmoBasePos.y(), gizmoBasePos.z()), QVector3D(gizmoBasePos.x(), gizmoBasePos.y() + 10000, gizmoBasePos.z())};
    arrowPoints.insert(EArrowType::YArrow, vertices);

    std::vector<IndexType> controlPointsIndices(vertices.size());
    for (int i = 0; i < vertices.size(); ++i)
        controlPointsIndices[i] = i;

    appendLines(indices, controlPointsIndices, false);

    assignLodLevel(ELOD::Last, data);

    createGizmoArrowsVbo();
}

void DManipulationGizmo::createGizmoArrowsVbo()
{
    xArrow = QSharedPointer<RenderGeometryData<>>::create();
    zArrow = QSharedPointer<RenderGeometryData<>>::create();

    std::vector<IndexType> indices({0, 1});

    xArrow->vertices.emplace_back(QVector3D(gizmoBasePos.x(), gizmoBasePos.y(), gizmoBasePos.z()));
    xArrow->vertices.emplace_back(QVector3D(gizmoBasePos.x() + 10000, gizmoBasePos.y(), gizmoBasePos.z()));

    zArrow->vertices.emplace_back(QVector3D(gizmoBasePos.x(), gizmoBasePos.y(), gizmoBasePos.z()));
    zArrow->vertices.emplace_back(QVector3D(gizmoBasePos.x(), gizmoBasePos.y(), gizmoBasePos.z() + 10000));

    xArrow->fillVbo();
    zArrow->fillVbo();

    appendLines(xArrow->indices, indices, false);
    appendLines(zArrow->indices, indices, false);

    arrowPoints.insert(EArrowType::XArrow, xArrow->vertices);
    arrowPoints.insert(EArrowType::ZArrow, zArrow->vertices);
}

QVector3D DManipulationGizmo::getMovementDelta(int mousePosX, int mousePosY)
{
    auto* camera = OmnigenCameraMgr::get()->getCameraForActiveViewport();
    auto&& cameraPos = camera->getPosition();
    auto rayVec = camera->makeRayFromCursor(mousePosX, mousePosY);

    std::array<QVector3D, 2> arrowVec;
    std::array<QVector3D, 2> camVec({ cameraPos, (cameraPos + (rayVec * 500000)) });
    float axisLength = 250000;

    switch (arrowSelected)
    {
    case EArrowType::YArrow: arrowVec = { QVector3D(gizmoBasePos.x(), gizmoBasePos.y() - axisLength, gizmoBasePos.z()), QVector3D(gizmoBasePos.x(), gizmoBasePos.y() + axisLength, gizmoBasePos.z()) }; break;
    case EArrowType::XArrow: arrowVec = { QVector3D(gizmoBasePos.x() - axisLength, gizmoBasePos.y(), gizmoBasePos.z()), QVector3D(gizmoBasePos.x() + axisLength, gizmoBasePos.y(), gizmoBasePos.z()) }; break;
    case EArrowType::ZArrow: arrowVec = { QVector3D(gizmoBasePos.x(), gizmoBasePos.y(), gizmoBasePos.z() - axisLength), QVector3D(gizmoBasePos.x(), gizmoBasePos.y(), gizmoBasePos.z() + axisLength) }; break;
    }

    return (std::get<1>(distance(camVec, arrowVec)) - gizmoBasePos);
}

void DManipulationGizmo::showAtPos(QVector3D newPos, bool drawX, bool drawY, bool drawZ)
{
    useArrow[EArrowType::XArrow] = drawX;
    useArrow[EArrowType::YArrow] = drawY;
    useArrow[EArrowType::ZArrow] = drawZ;

    gizmoOffset = newPos - gizmoBasePos;

    updateArrowPoints();

    bVisible = true;
}

QVector3D DManipulationGizmo::moveAlongAxis(int mousePosX, int mousePosY)
{
    auto moveDelta = getMovementDelta(mousePosX, mousePosY) - (mouseGripOffset + gizmoOffset);

    gizmoOffset += moveDelta;

    return moveDelta;
}
