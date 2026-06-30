#pragma once
#include "Scene/Generation/Common/Markers/BatchingMeshMarker.h"
#include "Utils/QuadTreeLite.h"

namespace Generation
{
    class TerrainBlockClusterBase;
}

// Sync with ClusterSmoothing.hlsl -> TerrainMeshVertex
#pragma pack(push, 4)
struct TerrainMeshVertex
{
    TerrainMeshVertex() = default;
    TerrainMeshVertex(const QVector3D& inPos, const QVector3D& inNormal, const Generation::TerrainBlockClusterBase& cluster);

    QVector3D position;
    QVector3D normal;

    quint32 terrainTexWeights;  // 4 pack ids
    quint32 biomeTexWeights;    // 4 pack ids
    quint32 packParams;         // terrain slot, grass slot, ?, ?

    float displacementFactor = 1.0f;
    float temperature;
    float humidity;
};
#pragma pack(pop)

struct ClusterMeshPainter;

template<>
constexpr bool serializeAsPOD<TerrainMeshVertex> = true;

struct ClusterMeshBatchParams : MeshBatchParams<TerrainMeshVertex>
{
    using MeshBatchParams<TerrainMeshVertex>::VertexType;
    using PainterType = ClusterMeshPainter;

    qint64 metaClusterGuid;

    bool operator<(const ClusterMeshBatchParams& other) const
    {
        return metaClusterGuid < other.metaClusterGuid;
    };

    bool operator==(const ClusterMeshBatchParams& other) const
    {
        return metaClusterGuid == other.metaClusterGuid;
    }
};

namespace std {
    template <> struct hash<ClusterMeshBatchParams>
    {
        size_t operator()(const ClusterMeshBatchParams& x) const
        {
            return hash<qint64>()(x.metaClusterGuid);
        }
    };
}

// Almost 1:1 copy of MeshPainter<TerrainMeshVertex>, except for "// Added"
struct ClusterMeshPainter
{
    using VertexType = TerrainMeshVertex;

    ShaderPipeline shaderPipeline;
    float lineWidthCached;

    quint32 getShaderLabel() const { return typeid(decltype(*this)).hash_code(); };

    virtual void createShader()
    {
        if (shaderPipeline.isLinked())
            return;

        // Compute final vertex position.
        std::ostringstream vSS;
        vSS << "#version " OPENGL_SHADER_VER "\n"

            << "uniform mat4 viewProjection;\n"
            << "uniform vec3 renderOffset;\n"
            << "in vec3 pos;\n";

        if constexpr (HasNormal<VertexType>)
        {
            vSS << "in vec3 normal;\n"
                << "out vec3 pNormal;\n";
        }

        vSS << "void main(void)\n"
            << "{\n"
            << "    gl_Position = viewProjection * vec4(pos + renderOffset, 1.0f);\n";

        if constexpr (HasNormal<VertexType>)
        {
            vSS << "pNormal = normal;\n";
        }

        vSS << "}";

        std::ostringstream fSS;
        fSS << "#version " OPENGL_SHADER_VER "\n"

            << "const vec3 lightDirection = vec3(0.57735026919f, -0.57735026919f, 0.57735026919f);\n"
            << "const float ambient = 0.2f;\n"

            << "uniform vec4 color;\n"
            << "uniform int objectID;\n";

        // Added
        fSS << "uniform int batchID;\n\n";

        if constexpr (HasNormal<VertexType>)
        {
            fSS << "in vec3 pNormal;\n";
        }

        fSS << "layout (location = 0) out vec4 fragColor;\n"
            << "layout (location = 1) out vec4 outData;\n\n"

            << "void main(void)\n"
            << "{\n";

        if constexpr (HasNormal<VertexType>)
        {
            fSS << "vec3 normal = normalize(pNormal);\n"
                << "float factor = clamp(dot(lightDirection, -normal), 0.0f, 1.0f - ambient) + ambient;\n"
                << "fragColor = vec4(color.rgb * factor, 1);\n";
        }
        else
        {
            fSS << "fragColor = color;\n";
        }

        fSS << "   outData = vec4(objectID, batchID, gl_PrimitiveID, 1); \n";
        fSS << "}";

        shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Vertex, vSS.str().data());
        shaderPipeline.addShaderFromSourceCode(QOpenGLShader::Fragment, fSS.str().data());
        bool ok = shaderPipeline.link();

        shaderPipeline.set(EShaderAttribute::Position, "pos");
        shaderPipeline.set(EShaderUniform::ViewProjectionMtx, "viewProjection");
        shaderPipeline.set(EShaderUniform::WorldMtx, "renderOffset");
        shaderPipeline.set(EShaderUniform::Color0, "color");

        if constexpr (HasNormal<VertexType>)
        {
            shaderPipeline.set(EShaderAttribute::Normal, "normal");
        }

        shaderPipeline.set(EShaderUniform::ObjectID, "objectID");

        // Added
        shaderPipeline.set(EShaderUniform::BatchID, "batchID");
    }

    void bindShader(const OmnigenCamera& camera)
    {
        shaderPipeline.bind();
        shaderPipeline.setUniformValue(EShaderUniform::ViewProjectionMtx, camera.getViewProjectionMatrix());
    }

    void draw(RenderGeometryData<VertexType>& geom, const MeshBatchParams<VertexType>& params, int batchID)
    {
        auto& vbo = geom.vbo;
        vbo.bind();

        // simple vertex type
        if constexpr (sizeof(VertexType) == sizeof(QVector3D))
        {
            ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, 0, 3, sizeof(VertexType));
        }
        // custom vertex type
        else
        {
            ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Position, GL_FLOAT, offsetof(VertexType, position), 3, sizeof(VertexType));

            if constexpr (HasNormal<VertexType>)
            {
                ShaderPipeline::current->setAttributeBuffer(EShaderAttribute::Normal, GL_FLOAT, offsetof(VertexType, normal), 3, sizeof(VertexType));
            }
        }
        vbo.release();

        ShaderPipeline::current->setUniformValue(EShaderUniform::Color0, params.color);
        ShaderPipeline::current->setUniformValue(EShaderUniform::WorldMtx, params.renderOffset);

        // Added
        ShaderPipeline::current->setUniformValue(EShaderUniform::BatchID, batchID);

        if (params.renderMode == ERenderType::Wireframe)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glGetFloatv(GL_LINE_WIDTH, &lineWidthCached);
            glLineWidth(3);
        }

        glDrawElements(params.primitiveType, geom.indices.size(), GL_UNSIGNED_INT, geom.indices.data());

        if (params.renderMode == ERenderType::Wireframe)
        {
            glPolygonMode(GL_FRONT, GL_FILL);
            glLineWidth(lineWidthCached);
        }
    }

    void unbindShader()
    {
        shaderPipeline.release();
    }

    virtual bool shouldDraw() const { return true; }

    // Added
    std::unordered_map<ClusterMeshBatchParams, std::vector<Generation::TerrainBlockClusterBase*>> trianglesToClusters;
};

// Altered draw function to pass in batchID
template<>
class DBatchingMarker<ClusterMeshBatchParams> : public DMarker
{
    using BatchParams = ClusterMeshBatchParams;
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
        int batchID = -1;
        for (auto&& [batchParams, batch] : batches)
        {
            // Critical section #3: Batch
            std::scoped_lock batchLock(batch.guard);
            ++batchID;
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

            painter.draw(*batch.geometry, batchParams, batchID);
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
                        auto&& p = vertices[i].position;
                        qTree->add_node(p.x(), p.z(), BatchedMarkerPoint{ section.get(), i - section->getVertexBufferOffset() });
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

    auto getBatches() const { return std::tie(batches, batchesGuard); };
    mutable ClusterMeshPainter painter;

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

    FRIEND_OMNIBIN_T(DBatchingMarker);
};

using DClusterMeshMarker = DBatchingMarker<ClusterMeshBatchParams>;
inline auto& gClusterMeshMarkerInstance = gBatchingMarkerInstance<ClusterMeshBatchParams>;