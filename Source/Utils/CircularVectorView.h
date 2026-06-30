#pragma once
#include <QVector>

template<template<typename...> typename C, typename T>
class CircularVectorView
{
public:
    CircularVectorView(const C<T>& vec)
        : data(vec)
        , offset(0)
        , size(vec.size())
    {}

    CircularVectorView(const C<T>& vec, int inOffset, int inSize)
        : data(vec)
        , offset(inOffset)
        , size(inSize)
    {}

    int getOffset() const { return offset; }
    int getSize() const { return size; }

    // Input index must be in view space [0..size]
    int findIdx(int idx, int adj) const
    {
        Q_ASSERT(idx < size);
        int i = idx + adj;

        if (i >= size)
            i -= size;
        else if (i < 0)
            i += size;

        return i;
    }

    const T& get(int idx, int adj) const
    {
        return data[findIdx(idx, adj)];
    }

    const T& getNext(int idx) const
    {
        return data[findIdx(idx, 1)];
    }

    const T& getPrev(int idx) const
    {
        return data[findIdx(idx, -1)];
    }


    int clamp(int v, int a, int b)
    {
        int dirA = closerDir(a, v);
        int dirB = closerDir(b, v);

        if (dirA != dirB)
        {
            return v;
        }
        else
        {
            if (dist(a, v) < dist(b, v))
                return a;
            else
                return b;
        }
    }

    int findMidpointShorter(int a, int b) const
    {
        int dCW = distCW(a, b);
        int dCCW = distCCW(a, b);

        if (dCW < dCCW)
            return findIdx(a, dCW / 2);
        else
            return findIdx(a, -dCCW / 2);
    }

    int findMidpointLonger(int a, int b) const
    {
        int dCW = distCW(a, b);
        int dCCW = distCCW(a, b);

        if (dCW < dCCW)
            return findIdx(a, -dCCW / 2);
        else
            return findIdx(a, dCW / 2);
    }

    int closerDir(int a, int b) const
    {
        if (distCW(a, b) < distCW(b, a))
            return 1;
        else
            return -1;
    }

    int dist(int a, int b) const
    {
        return std::min(distCW(a, b), distCCW(a, b));
    }

    template<typename BOOL>
    int dist(int a, int b, BOOL cw) const
    {
        static_assert(std::is_same_v<BOOL, bool>, "Use a boolean [true] for CW and [false] for CCW");
        return cw ? distCW(a, b) : distCCW(a, b);
    }

    int distCW(int a, int b) const
    {
        if (a <= b)
            return b - a;
        else
            return size + (b - a);
    }

    int distCCW(int a, int b) const
    {
        return distCW(b, a);
    }

    int getDirFromAToBThroughC(int a, int b, int c) const
    {
        int dir = closerDir(a, c);
        const int distAC = dist(a, c, dir > 0);
        const int distAB = dist(a, b, dir > 0);
        return distAB >= distAC ? dir : -dir;
    }

    template<typename L>
    void forRange(int a, int b, bool cw, L l) const
    {
        if (cw)
            forRangeCW(a, b, l);
        else
            forRangeCCW(a, b, l);
    }

    template<typename L>
    void forRangeCW(int a, int b, L l) const
    { 
        int dist = distCW(a, b);
        for (int i = 0; i <= dist; ++i)
            l(findIdx(a, i));
    }

    template<typename L>
    void forRangeCCW(int a, int b, L l) const
    {
        int dist = distCCW(a, b);
        for (int i = 0; i <= dist; ++i)
            l(findIdx(a, -i));
    }

    template<typename L>
    void forRangeShorter(int a, int b, L l) const
    {
        if (distCW(a, b) < distCCW(a, b))
            forRangeCW(a, b, l);
        else
            forRangeCCW(a, b, l);
    }

    template<typename L>
    void forRangeLonger(int a, int b, L l) const
    {
        if (distCW(a, b) < distCCW(a, b))
            forRangeCCW(a, b, l);
        else
            forRangeCW(a, b, l);
    }

    int getShortOffset(int a, int b) const
    {
        int cw = distCW(a, b);
        int ccw = distCCW(a, b);
        if (cw < ccw)
            return cw;
        else
            return -ccw;
    }

    int getLongOffset(int a, int b) const
    {
        int cw = distCW(a, b);
        int ccw = distCCW(a, b);
        if (cw < ccw)
            return -ccw;
        else
            return cw;
    }

    std::vector<int> sortIndices(const std::vector<int>& indices)
    {
        struct IndexNode
        {
            int idx;
            int dist;
            IndexNode* next = nullptr;
        };

        std::vector<IndexNode> nodes(indices.size());
        for (int i = 0; i < nodes.size(); ++i)
            nodes[i].idx = indices[i];

        int greatestCWDist = 0;
        IndexNode* greatestDistHolder = nullptr;
        for (auto&& node : nodes)
        {
            // Link node with the closest CW node
            int minD = data.size();
            for (auto&& otherNode : nodes)
                if (node.idx != otherNode.idx)
                    if (int d = distCW(node.idx, otherNode.idx); d < minD)
                    {
                        minD = d;
                        node.next = &otherNode;
                    }

            // Track the longest connection
            if (minD > greatestCWDist)
            {
                greatestCWDist = minD;
                greatestDistHolder = &node;
            }
        }
            
        IndexNode* outputNode = greatestDistHolder->next;
        std::vector<int> results;
        for (int i = 0; i < nodes.size(); ++i)
        {
            results << outputNode->idx;
            outputNode = outputNode->next;
        }

        return results;
    }

    const T& operator[](int i) const
    {
        return data[offset + i];
    }

    auto begin() const
    {
        return data.begin() + offset;
    }

    auto end() const
    {
        return begin() + size;
    }

private:
    const C<T>& data;
    const int offset;
    const int size;
};

template<template<typename...> typename C, typename T>
auto asCircular(const C<T>& vec) -> CircularVectorView<C, T>
{
    return CircularVectorView<C, T>(vec);
}