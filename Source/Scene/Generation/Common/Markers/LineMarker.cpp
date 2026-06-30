#include "stdafx.h"
#include "LineMarker.h"
#include "Editable.h"
#include "Editor/Sections/Viewport/OmnigenViewport.h"
#include "Source/Editor/Sections/Viewport/OmnigenCameraManager.h"

DLineMarker::DLineMarker(const std::vector<QVector3D>& inControlPoints, const QVector4D& inColor, bool inIsLoop, float inHeight)
    : color(inColor)
    , bIsLoop(inIsLoop)
    , height(inHeight)
{
    createDefaultLodLevel(inControlPoints);
}

DLineMarker::DLineMarker(const QVector3D& inControlPoint, const float inHeight, const QVector4D& inColor, bool inIsLoop)
    : color(inColor)
    , bIsLoop(inIsLoop)
    , height(0)
{
    createDefaultLodLevel(std::vector{ inControlPoint, inControlPoint + QVector3D(0, inHeight, 0) });
}

DLineMarker::DLineMarker(const QVector3D& p1, const QVector3D& p2, const QVector4D& inColor, float inHeight, ELineDecorator ld)
    : color(inColor)
    , bIsLoop(false)
    , height(inHeight)
{
    switch(ld)
    {
    case ELineDecorator::Arc:
        createDefaultLodLevel(std::vector{ p1, (p1 + p2) * 0.5 + QVector3D(0, distance(p1, p2), 0), p2 });
        break;
    case ELineDecorator::Arrow:
        float w = distance(p1, p2) * 0.2;
        auto r = p2 + QQuaternion::fromEulerAngles(0, 45, 0).rotatedVector(p1 - p2).normalized() * w;
        auto l = p2 + QQuaternion::fromEulerAngles(0, -45, 0).rotatedVector(p1 - p2).normalized() * w;
        createDefaultLodLevel(std::vector{ r, p2, p1, p2, l });
        break;
    }
}

DLineMarker::DLineMarker(const std::vector<GVector2D>& inControlPoints, const QVector4D& inColor /*= QVector4D(1, 1, 1, 1)*/, bool inIsLoop /*= false*/, float inHeight /*= 0.0f*/)
    : color(inColor)
    , bIsLoop(inIsLoop)
    , height(inHeight)
{
    createDefaultLodLevel({ inControlPoints.begin(), inControlPoints.end() });
}


DLineMarker::DLineMarker(const DLineMarker& other)
    : DMarker(other)
    , color(other.color)
    , bIsLoop(other.bIsLoop)
    , height(other.height)
    , length(other.length)
{
}


DLineMarker& DLineMarker::operator=(const DLineMarker& other)
{
    static_cast<DMarker&>(*this) = other;
    color = other.color;
    bIsLoop = other.bIsLoop;
    height = other.height;
    length = other.length;
    return *this;
}

void DLineMarker::initialize()
{
    DMarker::initialize();
    computeBoxVerts();
}

void DLineMarker::bindShader(const OmnigenCamera& camera)
{
    shaderPipeline.bind();
    shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
    glGetFloatv(GL_LINE_WIDTH, &lastLineWidth);
    glLineWidth(3);
}

void DLineMarker::cacheBoundingBox()
{
    cachedBoundingBox = BoundingBox::fromPoints(getControlPoints());
}

void DLineMarker::draw()
{
    auto& vbo = getActiveGeometry()->vbo;
    vbo.bind();
    ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
    vbo.release();

    auto&& usedColor = (bSelected || bHovered) && selectionColor ? *selectionColor : color;
    ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, usedColor);
    ShaderPipeline::current->setUniformValue(EShaderUniform::Height, height);

    auto& indices = getActiveGeometry()->indices;
    glDrawElements(GL_LINES, indices.size(), GL_UNSIGNED_INT, indices.data());

    if ((bHovered || bSelected) && selectionGeometry)
    {
        selectionGeometry->vbo.bind();
        ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, 3 * sizeof(GLfloat));
        selectionGeometry->vbo.release();

        glDrawElements(GL_LINES, selectionGeometry->indices.size(), GL_UNSIGNED_INT, selectionGeometry->indices.data());
    }

    DMarker::draw();
}

void DLineMarker::unbindShader()
{
    shaderPipeline.release();
    glLineWidth(lastLineWidth);
}

void DLineMarker::extendMarker(const QVector3D& p)
{
    auto& vertices = getGeometry<QVector3D>(ELOD::Last)->vertices;
    auto& indices = geometry[ELOD::Last]->indices;

    vertices.emplace_back(p);

    appendLines(indices, { IndexType(vertices.size() - 2), IndexType(vertices.size() - 1) });
    updateVbo(ELOD::Last);
}

void DLineMarker::setPoints(const std::vector<QVector3D>& newVerts, bool isLoop /*= false*/)
{
    auto& vertices = getActiveGeometry()->vertices;
    auto& indices = getActiveGeometry()->indices;

    bIsLoop = isLoop;
    vertices = newVerts;
    std::vector<IndexType> controlPointsIndices(newVerts.size());
    for (int i = 0; i < newVerts.size(); ++i)
        controlPointsIndices[i] = i;

    indices.clear();
    appendLines(indices, controlPointsIndices, bIsLoop);

    cacheBoundingBox();
    computeBoxVerts();
}

void DLineMarker::movePoints(const std::vector<QVector3D>& newVerts, int vertsAdded /*= 0*/)
{
    auto& verts = getActiveGeometry()->vertices;
    auto& indices = geometry[ELOD::Last]->indices;
    verts = newVerts;

    // Modify indices depending on points added or erased
    if(vertsAdded > 0)
    {
        for (int i = vertsAdded + 1; i > 1; i--)
            appendLines(indices, { IndexType(verts.size() - i), IndexType(verts.size() - (i - 1)) });
    }
    else if (vertsAdded < 0)
    {
        for (int i = vertsAdded; i < 0; i++)
        {
            indices.pop_back();
            indices.pop_back();
        }
    }

    updateVbo(activeLOD);
    cacheBoundingBox();
    computeBoxVerts();
}

void DLineMarker::setSelected(bool b)
{
    bSelected = b;
}

void DLineMarker::setHovered(bool b)
{
    bHovered = b;
}

float DLineMarker::getLength() const
{
    std::scoped_lock lock(lengthGuard);

    if (!length)
        computeLength();

    return *length;
}

void DLineMarker::createShader()
{
    if (shaderPipeline.isLinked())
        return;

    // Compute final vertex position.
    const char* vertexShaderSource =
        "#version " OPENGL_SHADER_VER "\n"
        "uniform mediump mat4 viewProjection;\n"
        "uniform float height;\n\n"

        "in vec4 vertex;\n\n"

        "void main(void)\n"
        "{\n"
        "    vec4 pos = vertex;\n"
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

    shaderPipeline.set(EShaderAttribute::Position, "vertex");
    shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
    shaderPipeline.set(EShaderUniform::Color0, "color");
    shaderPipeline.set(EShaderUniform::Height, "height");

    shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
}

void DLineMarker::createDefaultLodLevel(const std::vector<QVector3D>& controlPoints)
{
    auto data = QSharedPointer<RenderGeometryData<>>::create();
    auto& vertices = data->vertices;
    auto& indices = data->indices;

    vertices = controlPoints;
    std::vector<IndexType> controlPointsIndices(controlPoints.size());
    for (int i = 0; i < controlPoints.size(); ++i)
        controlPointsIndices[i] = i;

    appendLines(indices, controlPointsIndices, bIsLoop);

    assignLodLevel(ELOD::Last, data);
}


void DLineMarker::computeBoxVerts()
{
    // TODO: Should probably use bounding box
    float offsetLen = 1000;
    const auto& controlPoints = getControlPoints();

    if (controlPoints.size() < 2)
        return;

    auto offset = [&](int i, int dir, bool isX) 
    {
        int j = i - 1;
        int k = i + 1;

        // For better results vector between i-1 and i+1 is taken, but edge cases need to be accommodated
        if (i == 0)
            j = 0;

        if (i == controlPoints.size() - 1)
        {
            k = i;
            j = i - 1;
        }

        GVector2D offsetVec = { (controlPoints[k].x() - controlPoints[j].x()), (controlPoints[k].z() - controlPoints[j].z()) };
        offsetVec.normalize();
        GVector2D offsetR = offsetVec.rotatedRight90();
        GVector2D offsetL = offsetVec.rotatedLeft90();

        if (isX)
            return (dir <= 1 ? offsetL.x * offsetLen : offsetR.x * offsetLen);
        else
            return (dir <= 1 ? offsetL.z * offsetLen : offsetR.z * offsetLen);
    };

    selectionGeometry = QSharedPointer<RenderGeometryData<>>::create();
    std::vector<std::vector<IndexType>> indices(4, std::vector<IndexType>(controlPoints.size()));

    for (int j = 0; j <= 3; j++)
        for (int i = 0; i < controlPoints.size(); i++)
        {
            auto v = QVector3D(
                controlPoints[i].x() + offset(i, j, true),
                controlPoints[i].y() + ((j == 1 || j == 2) ? (offsetLen * (-1)) : offsetLen),
                controlPoints[i].z() + offset(i, j, false));

            selectionGeometry->vertices.emplace_back(v);
            indices[j][i] = selectionGeometry->vertices.size() - 1;
        }

    selectionGeometry->fillVbo();

    int s = indices[0].size() - 1;
    appendLines(selectionGeometry->indices, { indices[0][0], indices[1][0], indices[2][0], indices[3][0] }, true);
    appendLines(selectionGeometry->indices, { indices[0][s], indices[1][s], indices[2][s], indices[3][s] }, true);

    for (int i = 0; i < 4; i++)
        appendLines(selectionGeometry->indices, indices[i]);
}

const std::vector<QVector3D>& DLineMarker::getControlPoints() const
{
    return getGeometry<QVector3D>(ELOD::Last)->vertices;
}

std::vector<GVector2D> DLineMarker::get2DPoints() const
{
    auto&& pts = getControlPoints();
    return std::vector<GVector2D>(pts.begin(), pts.end());
}

void DLineMarker::computeLength() const
{
    auto&& pts = getControlPoints();
    length = 0.0f;

    for (int i = 1; i < pts.size(); ++i)
        *length += distance(pts[i],pts[i - 1]);
}

DCircleMarker::DCircleMarker(const GVector2D& center, float radius, const QVector4D& inColor, float inHeight)
{
    color = inColor;
    bIsLoop = true;
    height = 0.f;

    constexpr int count = 42;
    std::vector<QVector3D> pts;
    pts.reserve(42);
    for (int i = 0; i < count; ++i)
    {
        const float angle = degToRad(i * 360.f / count);
        pts << QVector3D(center.x + radius * fastCos(angle), inHeight, center.z + radius * fastSin(angle));
    }

    createDefaultLodLevel(std::move(pts));
}

DMultiLineMarker::DMultiLineMarker(const std::vector<std::vector<QVector3D>>& inLines, const QVector4D& inColor /*= QVector4D(1, 1, 1, 1)*/, float inHeight)
{
    auto data = QSharedPointer<RenderGeometryData<>>::create();
    auto& vertices = data->vertices;
    auto& indices = data->indices;

    for (auto&& line : inLines)
    {
        int offset = vertices.size();
        vertices << line;

        std::vector<IndexType> lineIndices(line.size());
        for (int i = 0; i < line.size(); ++i)
            lineIndices[i] = offset + i;

        appendLines(indices, lineIndices);
    }

    assignLodLevel(ELOD::Last, data);

    height = inHeight;
    color = inColor;
}


void DMultiLineMarker::initialize()
{
    DMarker::initialize();
}

template<typename LinePointType>
int getLineSideImpl(const std::vector<LinePointType>& pts, int idx, const GVector2D& point)
{
    GVector2D pointDir = (point - GVector2D(pts[idx])).normalized();
    const int lastIdx = pts.size() - 1;
    int idxA = (idx == lastIdx) ? lastIdx - 1 : idx;
    int idxB = (idx == lastIdx) ? lastIdx : idx + 1;
    while (true)
    {
        auto seaDir = GVector2D(pts[idxB] - pts[idxA]).normalized();

        // Return only if the segment is not parallel to the shore--point line
        if (float sineValue = GVector2D::crossProduct(seaDir, pointDir); qAbs(sineValue) > 0.2f)
            return (sineValue < 0.0f) ? 1 : -1;

        // Extend lookup to find a nonparallel segment
        if (idxB < pts.size() - 1)
            ++idxB;
        else if (idxA > 0)
            --idxA;
        else
            return 0;
    }

    Q_ASSERT(false);
    return 0;
}

int getLineSide(const LineMarkerPoint& closestLinePoint, const GVector2D& point)
{
    if (!closestLinePoint)
        return 0;

    // Pick better facing segment
    int idx = closestLinePoint.idx;
    auto&& pts = closestLinePoint.marker->getControlPoints();
    float minDist = std::numeric_limits<float>::max();
    for (size_t i = 0; i < pts.size() - 1; ++i)
    {
        if (auto d = std::get<float>(distance({ GVector2D(pts[i]), GVector2D(pts[i + 1]) }, point)); d < minDist)
        {
            minDist = d;
            idx = i;
        }
    }
    return getLineSideImpl(pts, idx, point);
}

int getLineSide(const std::vector<GVector2D>& pts, const GVector2D& point)
{
    auto [closestLinePoint, d, idx] = directionalBoundDistance(pts, point, true);
    return getLineSideImpl(pts, idx, point);
}
