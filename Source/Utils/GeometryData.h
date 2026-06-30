#pragma once
#include <vector>
#include <QVector3D>
#include <QMap>
#include <QOpenGLBuffer>
#include "Constants.h"
#include "Utils/OmniBin/OmniBinQt.h"
#include <tbb/parallel_for.h>

struct GeometryDataBase
{
    virtual void fillVbo() {};
    virtual void clearVbo() {};
    virtual int size() const { return 0; }

    virtual quint32 instanceCount() const { return 0; }
    virtual quint32 visibleInstanceCount() const { return 0; }
    virtual int instanceSize() const { return 0; }
    virtual const QMatrix4x4& getInstanceTransform(int idx) const { static const QMatrix4x4 m; return m; }
    virtual void setVisibleInstances(const std::vector<quint32>& indices) const {};

	std::vector<IndexType> indices;
};

inline void omniSave(const GeometryDataBase& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.indices;
}

inline void omniLoad(GeometryDataBase& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.indices;
}

template<typename PointType = QVector3D>
struct GeometryData : GeometryDataBase
{
    std::vector<PointType> vertices;

    virtual int size() const override
    {
        return sizeof(PointType);
    }

    void clear()
    {
        vertices.clear();
        indices.clear();
    }
};

template<typename PT>
inline void omniSave(const GeometryData<PT>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const GeometryDataBase&>(object);
    omniBin << object.vertices;
}

template<typename PT>
inline void omniLoad(GeometryData<PT>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<GeometryDataBase&>(object);
    omniBin >> object.vertices;
}

template<typename PointType = QVector3D>
struct RenderGeometryData : GeometryData<PointType>
{
    virtual void fillVbo() override
    {
        if (!vbo.isCreated())
        {
            bool createPassed = vbo.create();
            Q_ASSERT(createPassed);
        }

        bool bindPassed = vbo.bind();
        Q_ASSERT(bindPassed);

        vbo.allocate(this->vertices.data(), this->vertices.size() * GeometryData<PointType>::size());
        vbo.release();
    }

    QOpenGLBuffer vbo;
};

template<typename PT>
inline void omniSave(const RenderGeometryData<PT>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const GeometryData<PT>&>(object);
}

template<typename PT>
inline void omniLoad(RenderGeometryData<PT>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<GeometryData<PT>&>(object);
}

template<typename PointType, typename InstanceData>
struct InstancedRenderGeometryData : RenderGeometryData<PointType>
{
    InstancedRenderGeometryData()
    {
        instanceBuffer.setUsagePattern(QOpenGLBuffer::UsagePattern::DynamicDraw);
    }

    virtual quint32 instanceCount() const override
    {
        return instanceData.size();
    }

    virtual quint32 visibleInstanceCount() const override
    { 
        return visibleInstanceData.size();
    }

    virtual int instanceSize() const override
    {
        return sizeof(InstanceData);
    }

    virtual const QMatrix4x4& getInstanceTransform(int idx) const override
    {
        // Assumes InstanceData type begins with QMatrix4x4 XD
        return reinterpret_cast<const QMatrix4x4&>(instanceData[idx]);
    }

    virtual void setVisibleInstances(const std::vector<IndexType>& visibleInstanceIndices) const override
    {
        visibleInstanceData = visibleInstanceIndices;
        updateInstanceBuffer();
    };

    std::vector<InstanceData> instanceData;
    mutable std::vector<IndexType> visibleInstanceData;
    mutable QOpenGLBuffer instanceBuffer;

private:
    void updateInstanceBuffer() const
    {
        if (!instanceBuffer.isCreated())
        {
            bool createPassed = instanceBuffer.create();
            Q_ASSERT(createPassed);
        }

        bool bindPassed = instanceBuffer.bind();
        Q_ASSERT(bindPassed);

        instanceBuffer.allocate(this->visibleInstanceData.data(), this->visibleInstanceData.size() * sizeof(IndexType));
        instanceBuffer.release();
    }
};

template<typename PT, typename ID>
inline void omniSave(const InstancedRenderGeometryData<PT, ID>& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << static_cast<const RenderGeometryData<PT>&>(object);
    // Do not save instanceData here, as some of these structs are stored in Assets and we DO NOT want persistent instances there.
}

template<typename PT, typename ID>
inline void omniLoad(InstancedRenderGeometryData<PT, ID>& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> static_cast<RenderGeometryData<PT>&>(object);
    // Do not load instanceData here, as some of these structs are stored in Assets and we DO NOT want persistent instances there.
}