#pragma once

// Simple AABB
struct BoundingBox
{
    BoundingBox() = default;
    BoundingBox(const QVector3D& p, const QVector3D s) : nbl(p), sizes(s) {};
    QVector3D nbl;
    QVector3D sizes;

    // Generate a bounding box from a vector of any kind of vertex struct
    template<typename Point, typename PosGeter>
    static BoundingBox fromPoints(const std::vector<Point>& pts, const PosGeter& getPos, IndexType offset = 0, std::optional<IndexType> size = {})
    {
        BoundingBox result;

        auto& nbl = result.nbl;
        nbl = QVector3D(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        QVector3D ftr(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

        IndexType end = size ? (offset + *size) : pts.size();
        for (IndexType i = offset; i < end; ++i)
        {
            auto&& point = getPos(pts[i]);

            if (point.x() < nbl.x()) nbl.setX(point.x());
            if (point.y() < nbl.y()) nbl.setY(point.y());
            if (point.z() < nbl.z()) nbl.setZ(point.z());

            if (point.x() > ftr.x()) ftr.setX(point.x());
            if (point.y() > ftr.y()) ftr.setY(point.y());
            if (point.z() > ftr.z()) ftr.setZ(point.z());
        }

        result.sizes = ftr - nbl;
        return result;
    }

    static BoundingBox fromPoints(const std::vector<QVector3D>& pts)
    {
        static const auto defaultGeter = [](const QVector3D& v) -> const QVector3D& { return v; };
        return fromPoints(pts, defaultGeter);
    }

    static BoundingBox merge(const BoundingBox& BB1, const BoundingBox& BB2);

    GVector2D getBottomLeft() const { return nbl; };
    GVector2D getTopLeft() const { return GVector2D(nbl.x(), nbl.z() + sizes.z()); };
    GVector2D getTopRight() const { return GVector2D(nbl.x() + sizes.x(), nbl.z() + sizes.z()); };
    GVector2D getBottomRight() const { return GVector2D(nbl.x() + sizes.x(), nbl.z()); };

    std::vector<QVector3D> getVertices() const { return { getBottomLeft(), getTopLeft(), getTopRight(), getBottomRight() }; };

    BoundingBox getScaledRetainingCenter(const float scale) const
    {
        const auto newSizes = sizes * scale;
        const auto newNbl = nbl + (sizes - newSizes) / 2;

        return BoundingBox(newNbl, newSizes);
    }

    BoundingBox transformed(const QMatrix4x4& transform) const;

    inline QVector3D getCenter() const
    {
        return nbl + sizes * 0.5f;
    }

    bool overlaps(const BoundingBox& bb) const
    {
        return !(nbl.x() > (bb.nbl.x() + bb.sizes.x()) || bb.nbl.x() > (nbl.x() + sizes.x()) || nbl.z() > (bb.nbl.z() + bb.sizes.z()) || bb.nbl.z() > (nbl.z() + sizes.z()));
    }

    bool contains(const QVector3D& p) const
    {
        return !(p.x() < nbl.x() || p.x() > nbl.x() + sizes.x() || p.z() < nbl.z() || p.z() > nbl.z() + sizes.z());
    }

    bool contains(const BoundingBox& bb) const
    {
        return nbl.x() <= bb.nbl.x() && (nbl.x() + sizes.x()) >= (bb.nbl.x() + bb.sizes.x()) && nbl.z() <= bb.nbl.z() && (nbl.z() + sizes.z()) >= (bb.nbl.z() + bb.sizes.z());
    }

    float dist(const QVector3D& p) const
    {
        float dx = std::max({ nbl.x() - p.x(), 0.0f, p.x() - (nbl.x() + sizes.x()) });
        float dz = std::max({ nbl.z() - p.z(), 0.0f, p.z() - (nbl.z() + sizes.z()) });
        if (float dist = std::sqrtf(dx * dx + dz * dz); dist != 0)
            return dist;
        else
            return std::min({ p.x() - nbl.x(), (nbl.x() + sizes.x()) - p.x(), p.z() - nbl.z(), (nbl.z() + sizes.z()) - p.z() });
    }

    void expandToContain(const QVector3D& p);

    void show();

    friend inline bool operator== (const BoundingBox& bb1, const BoundingBox& bb2) { return bb1.nbl == bb2.nbl && bb1.sizes == bb2.sizes; }
};