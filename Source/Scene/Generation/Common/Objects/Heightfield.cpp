#include "stdafx.h"
#include "Heightfield.h"
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>

namespace Generation
{
    Heightfield::Heightfield(const GVector2D& minXZ, const GVector2D& maxXZ, float inSpacing)
        : config(Config(minXZ, maxXZ, inSpacing))
    {
        geometry->vertices.resize((config.sizeX + 1) * (config.sizeZ + 1));

        auto& quads = geometry->indices;
        for (int x = 0; x < config.sizeX; x++) {
            for (int z = 0; z < config.sizeZ; z++) {
                quads << idx(x, z);
                quads << idx(x + 1, z);
                quads << idx(x + 1, z + 1);
                quads << idx(x, z + 1);
            }
        }
    }


    IndexType Heightfield::idx(int x, int z) const
    {
        return x + (config.sizeX + 1) * z;
    }

    IndexType Heightfield::idx(const GVector2D& point) const
    {
        return idx(point.x / config.gridSpacing - config.offsetX, point.z / config.gridSpacing - config.offsetZ);
    }

    GPoint Heightfield::fromIdx(IndexType i) const
    {
        return { int(i) % (config.sizeX + 1), int(i) / (config.sizeX + 1) };
    }


    float Heightfield::getHeightByIdx(IndexType i) const
    {
        return geometry->vertices[i].height;
    }

    QVector3D Heightfield::computeNormal(int x, int z) const
    {
        auto&& p = getPoint(x, z);
        QVector3D normal;

        //if (x > 0 && z > 0)
        {
            auto n = computeFaceNormal({ p, getPoint(x - 1, z), getPoint(x, z - 1) });
            normal += n;
        }
        //if (x > 0 && z < (heightData.getSize().z - 1))
        {
            auto n = -computeFaceNormal({ p, getPoint(x - 1, z), getPoint(x, z + 1) });
            normal += n;
        }
        //if (x < (heightData.getSize().x - 1) && z > 0)
        {
            auto n = -computeFaceNormal({ p, getPoint(x + 1, z), getPoint(x, z - 1) });;
            normal += n;
        }
        //if (x < (heightData.getSize().x - 1) && z < (heightData.getSize().z - 1))
        {
            auto n = computeFaceNormal({ p, getPoint(x + 1, z), getPoint(x, z + 1) });
            normal += n;
        }

        return normal.normalized();
    }

    BoundingBox Heightfield::getBoundingBox() const
    {
        QVector3D p0 = getPoint(0, 0);
        QVector3D pLast = getPoint(config.sizeX, config.sizeZ);
        return BoundingBox(p0, pLast - p0);
    }

    std::vector<float> Heightfield::createGDALbuffer(int margin)
    {
        // the +6 is for the margin required by GDALs contour generation, while the +1 is so that the whole DEM is taken
        config.edited = false;
        int sizeXwithMargin = config.sizeX + (margin * 2) + 1;
        int sizeZwithMargin = config.sizeZ + (margin * 2) + 1;

        GVector2D startingPoint = { (config.offsetX - 3) * config.gridSpacing, (config.offsetZ - 3) * config.gridSpacing };

        std::vector<float> result((sizeXwithMargin) * (sizeZwithMargin));
        auto&& gridPoints = geometry->vertices;

        for (int x = 0; x < sizeXwithMargin; ++x)
        {
            for (int z = 0; z < sizeZwithMargin; ++z)
            {
                GVector2D point = { (x * config.gridSpacing) + startingPoint.x, (z * config.gridSpacing) + startingPoint.z };
                int bufferIdx = x + (sizeXwithMargin * z);
                result[bufferIdx] = sample(point);
            }
        }

        return result;
    }

    void Heightfield::setHeight(int x, int z, float height)
    {
        setHeight(idx(x, z), height);
    }

    void Heightfield::setHeight(IndexType i, float height)
    {
        geometry->vertices[i].height = height;
    }

    void Heightfield::addHeight(IndexType i, float deltaH)
    {
        geometry->vertices[i].height += deltaH;
    }

    void Heightfield::setNormal(int x, int z, const QVector3D& normal)
    {
        setNormal(idx(x, z), normal);
    }

    void Heightfield::setNormal(IndexType i, const QVector3D& normal)
    {
        geometry->vertices[i].normal = normal;
    }

    float Heightfield::sample(const GVector2D& position) const
    {
        GVector2D local = getCoords(position);

        int x = std::floor(local.x);
        int z = std::floor(local.z);

        float xt = local.x - x;
        float zt = local.z - z;

        return std::lerp(
            std::lerp(sampleGrid(x, z), sampleGrid(x + 1, z), xt),
            std::lerp(sampleGrid(x, z + 1), sampleGrid(x + 1, z + 1), xt),
            zt
        );
    }

    float Heightfield::sampleSmooth(const GVector2D& position) const
    {
        GVector2D local = getCoords(position);

        int x = std::floor(local.x);
        int z = std::floor(local.z);

        float xt = local.x - x;
        float zt = local.z - z;

        // 16 DEM points around sample position
        float g[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                g[i][j] = sampleGrid(x - 1 + i, z - 1 + j);

        // values interpolated across 4 horizontal lines
        float hor[4];
        for (int i = 0; i < 4; i++)
            hor[i] = cubicInterpolation(g[0][i], g[1][i], g[2][i], g[3][i], xt);

        return cubicInterpolation(hor[0], hor[1], hor[2], hor[3], zt);
    }

    GVector2D Heightfield::sampleGradient(const GVector2D& position) const
    {
        GVector2D local = getCoords(position);

        int x = std::floor(local.x);
        int z = std::floor(local.z);

        float xt = local.x - x;
        float zt = local.z - z;

        auto h00 = sampleGrid(x + 0, z + 0);
        auto h10 = sampleGrid(x + 1, z + 0);
        auto h11 = sampleGrid(x + 1, z + 1);
        auto h01 = sampleGrid(x + 0, z + 1);

        float d = h11 - h00;
        return GVector2D(d + h10 - h01, d + h01 - h10).normalized();
    }

    QVector3D Heightfield::sampleNormal(const GVector2D& position) const
    {
        GVector2D local = getCoords(position);

        int x = std::floor(local.x);
        int z = std::floor(local.z);

        QVector3D normal;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                normal += getNormal(x + i, z + j);

        return normal.normalized();
    }

    void Heightfield::reshapeGrid(const GVector2D& minXZ, const GVector2D& maxXZ, float inSpacing)
    {
        auto toOldIdx = [&](IndexType i, const Config& oldConfig, const Config& newConfig)
        {
            auto x = int(i) % (newConfig.sizeX + 1) + newConfig.offsetX - oldConfig.offsetX;
            auto z = int(i) / (newConfig.sizeX + 1) + newConfig.offsetZ - oldConfig.offsetZ;
            bool withinBounds = (x >= 0 && x < oldConfig.sizeX + 1 && z >= 0 && z < oldConfig.sizeZ + 1);
            return withinBounds ? x + (oldConfig.sizeX + 1) * z : -1;
        };

        Config oldConfig = config;
        std::vector<Vertex> oldVertices = geometry->vertices;

        geometry->clear();

        config = Config(minXZ, maxXZ, inSpacing);
        geometry->vertices.resize((config.sizeX + 1) * (config.sizeZ + 1));

        tbb::parallel_for(0, int(geometry->vertices.size()), [&](int i)
            {
                if (auto oldIdx = toOldIdx(i, oldConfig, config); oldIdx >= 0 && oldIdx < oldVertices.size())
                    geometry->vertices[i] = oldVertices[oldIdx];
            });

        auto& quads = geometry->indices;
        for (int x = 0; x < config.sizeX; x++) {
            for (int z = 0; z < config.sizeZ; z++) {
                quads << idx(x, z);
                quads << idx(x + 1, z);
                quads << idx(x + 1, z + 1);
                quads << idx(x, z + 1);
            }
        }
    }

    void Heightfield::update() const
    {
        geometry->fillVbo();
    }

    void Heightfield::update(const std::vector<GPoint>& pts) const
    {
        auto* gl = QOpenGLContext::currentContext()->extraFunctions();
        geometry->vbo.bind();

        for (auto&& p : pts)
        {
            IndexType i = idx(p.x, p.z);
            gl->glBufferSubData(GL_ARRAY_BUFFER, i * geometry->size(), geometry->size(), &geometry->vertices[i]);
        }

        geometry->vbo.release();
    }


    void Heightfield::updateNormals()
    {
        tbb::parallel_for(tbb::blocked_range2d<int, int>(0, getSize().x, 0, getSize().z), [&](tbb::blocked_range2d<int>& r)
            {
                for (int z = r.cols().begin(); z <= r.cols().end(); ++z)
                    for (int x = r.rows().begin(); x <= r.rows().end(); ++x)
                        setNormal(x, z, computeNormal(x, z));
            });
    }

    QVector3D Heightfield::getPoint(int x, int z) const
    {
        QVector3D result(getPoint2D(x, z));
        result.setY(sampleGrid(x, z));
        return result;
    }


    const QVector3D& Heightfield::getNormal(int x, int z) const
    {
        auto&& gridPoints = geometry->vertices;

        int clampedX = std::clamp(x, 0, config.sizeX);
        int clampedZ = std::clamp(z, 0, config.sizeZ);

        return gridPoints[idx(clampedX, clampedZ)].normal;
    }

    GVector2D Heightfield::getPoint2D(int x, int z) const
	{
        return GVector2D{ float(x + config.offsetX), float(z + config.offsetZ) } * config.gridSpacing;
	}


    GVector2D Heightfield::getCoords(const GVector2D& position) const
    {
        return position / config.gridSpacing - GVector2D(config.offsetX, config.offsetZ);
    }

    float Heightfield::sampleGrid(int x, int z) const
    {
        auto&& gridPoints = geometry->vertices;

        int clampedX = std::clamp(x, 0, config.sizeX);
        int clampedZ = std::clamp(z, 0, config.sizeZ);
       
        return gridPoints[idx(clampedX, clampedZ)].height;
    }

    Heightfield::Config::Config(const GVector2D& minXZ, const GVector2D& maxXZ, float inSpacing)
        : gridSpacing(inSpacing)
        , sizeX((maxXZ.x + 1 - minXZ.x) / inSpacing)
        , sizeZ((maxXZ.z + 1 - minXZ.z) / inSpacing)
        , offsetX(minXZ.x / inSpacing)
        , offsetZ(minXZ.z / inSpacing)
    {
    }

    DHeightfieldMarker::DHeightfieldMarker(const QSharedPointer<RenderGeometryData<Heightfield::Vertex>>& inGeom, const Heightfield::Config& inConfig, const QVector4D& inColor)
        : geometry(inGeom)
        , color(inColor)
        , config(inConfig)
    {
        assignLodLevel(ELOD::Last, geometry);
    }

    void DHeightfieldMarker::cacheBoundingBox()
    {
        cachedBoundingBox.nbl = QVector3D(config.offsetX * config.gridSpacing, 0, config.offsetZ * config.gridSpacing);
        cachedBoundingBox.sizes = QVector3D(config.sizeX * config.gridSpacing, 100'0000, config.sizeZ * config.gridSpacing);
    }

    void DHeightfieldMarker::bindShader(const OmnigenCamera& camera)
    {
        shaderPipeline.bind();
        shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());

        glGetFloatv(GL_LINE_WIDTH, &lineWidthCached);
        glLineWidth(3);
    }

    void DHeightfieldMarker::draw()
    {
        auto& vbo = geometry->vbo;
        vbo.bind();
        ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, offsetof(Heightfield::Vertex, height), 1, sizeof(Heightfield::Vertex));
        ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Normal, GL_FLOAT, offsetof(Heightfield::Vertex, normal), 3, sizeof(Heightfield::Vertex));
        vbo.release();

        ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, color);
        ShaderPipeline::current->setUniformValue(EShaderUniform::Texture0, config.gridSpacing);
        ShaderPipeline::current->setUniformValue(EShaderUniform::Texture1, QVector2D(config.offsetX, config.offsetZ));
        ShaderPipeline::current->setUniformValue(EShaderUniform::Texture2, QVector2D(config.sizeX, config.sizeZ));

#if !DEBUG_HEIGHTFIELD_TEXCOORD
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif

        auto&& quads = geometry->indices;
        glDrawElements(GL_QUADS, quads.size(), GL_UNSIGNED_INT, quads.data());

        glPolygonMode(GL_FRONT, GL_FILL);
    }

    void DHeightfieldMarker::unbindShader()
    {
        shaderPipeline.release();
        glLineWidth(lineWidthCached);
    }

    void DHeightfieldMarker::createShader()
    {
        if (shaderPipeline.isLinked())
            return;

        // Compute final vertex position.
        const char* vertexShaderSource =
            "#version " OPENGL_SHADER_VER "\n"
            "uniform mat4 viewProjection;\n\n"
            "uniform float gridSpacing;\n"
            "uniform vec2 offset;\n"
            "uniform vec2 size;\n"

            "in float height;\n"
            "in vec3 normal;\n\n"

            "out vec3 pNormal;\n\n"
#if DEBUG_HEIGHTFIELD_TEXCOORD
            "out float coord;\n\n"
#endif

            "void main(void)\n"
            "{\n"
            "   int z = gl_VertexID / (int(size[0]) + 1);\n"
            "   int x = int(mod(double(gl_VertexID), double(size[0]) + 1.0));\n"
#if !DEBUG_HEIGHTFIELD_TEXCOORD
            "   gl_Position = viewProjection * vec4(float(offset[0] + x) * gridSpacing, height, (offset[1] + z) * gridSpacing, 1);\n"
            "   pNormal = normal;\n"
#else
            "   gl_Position = viewProjection * vec4(float(offset[0] + x) * gridSpacing, 0, (offset[1] + z) * gridSpacing, 1);\n"
            "   coord = abs(0.5f - height) * 2.0f;\n"
#endif
            "}\n";

        // The final color depends on the height. Simple, but brings a lot.
        const char* fragmentShaderSource =
            "#version " OPENGL_SHADER_VER "\n"

            "const vec3 lightDirection = vec3(0.57735026919f, -0.57735026919f, 0.57735026919f);\n"
            "const float ambient = 0.2f;\n\n"

            "uniform vec4 color;\n"
            "uniform int objectID;\n\n"

            "in vec3 pNormal;\n\n"

            "layout (location = 0) out vec4 fragColor;\n"
            "layout (location = 1) out vec4 outData;\n\n"

#if DEBUG_HEIGHTFIELD_TEXCOORD
            "in float coord;\n\n"
#endif

            "void main(void)\n"
            "{\n"
            "   fragColor = color;\n"
            "   outData = vec4(objectID, 0, gl_PrimitiveID, 1);\n"
#if DEBUG_HEIGHTFIELD_TEXCOORD
            "   fragColor = color * coord;\n"
            "   fragColor.a = 1;\n"
#else
            "   float factor = dot(lightDirection, -pNormal);\n"
            "   factor = clamp(factor, 0.0f, 1.0f - ambient) + ambient;\n"
            "   fragColor.xyz = fragColor.xyz * factor;\n"
            "   fragColor.w = 1;\n"
#endif
            "};\n";

        shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
        shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
        bool ok = shaderPipeline.link();

        shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
        shaderPipeline.set(EShaderUniform::Color0, "color");
        shaderPipeline.set(EShaderUniform::Texture0, "gridSpacing");
        shaderPipeline.set(EShaderUniform::Texture1, "offset");
        shaderPipeline.set(EShaderUniform::Texture2, "size");

        shaderPipeline.set(EShaderAttribute::Position, "height");
        shaderPipeline.set(EShaderAttribute::Normal, "normal");

        shaderPipeline.set(EShaderUniform::ObjectID, "objectID");
    }
}


