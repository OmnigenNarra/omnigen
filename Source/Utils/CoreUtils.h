#pragma once
#include <vector>
#include <compare>
#include "magic_enum.hpp"
#include <Mathematics/Vector.h>
#include "Mathematics/Vector3.h"
#include <Mathematics/BezierCurve.h>
#include "Triangulation/Earcut.hpp"
#include <Mathematics/BSplineCurveFit.h>
#include "CircularVectorView.h"

struct Segment2D;
struct GVector2D;

class QLayout;

qint64 makeGuid();
void appendFace(std::vector<IndexType>& allIndices, const std::vector<IndexType>& vertexIndices, bool backface = false);
void appendLines(std::vector<IndexType>& allIndices, const std::vector<IndexType>& lineIndices, bool loop = false);

template<typename T, typename V>
inline std::vector<T>& operator<<(std::vector<T>& vec, const V& val)
{
    vec.push_back(val);
    return vec;
}

template<typename T, typename V>
inline std::vector<T>& operator<<=(std::vector<T>& vec, V&& val)
{
    vec.push_back(std::move(val));
    return vec;
}

template<typename T>
inline std::vector<T>& operator<<(std::vector<T>& vec, const std::vector<T>& val)
{
    vec.insert(vec.end(), val.begin(), val.end());
    return vec;
}

template<typename T, typename V>
inline void removeOne(std::vector<T>& vec, const V& val)
{
    if (auto it = std::find(vec.begin(), vec.end(), val); it != vec.end())
        vec.erase(it);
}

template<typename T, typename V>
[[nodiscard]] inline std::vector<T> removeAll(std::vector<T>& vec, const V& val)
{
    std::vector<T> result;
    result.reserve(vec.size());
    for (auto&& v : vec)
        if (v != val)
            result.push_back(v);

    return result;
}

template<typename T>
[[nodiscard]] inline std::vector<T> filter(const std::vector<T>& vec, auto lambda)
{
    std::vector<T> result;
    result.reserve(vec.size());
    for (auto&& v : vec)
        if (lambda(v))
            result.push_back(v);

    return result;
}

template<typename T, typename V>
inline int indexOf(const std::vector<T>& vec, const V& val)
{
    for (int i = 0; i < vec.size(); ++i)
        if (vec[i] == val)
            return i;

    return -1;
}

template<typename T, typename V>
inline int lastIndexOf(const std::vector<T>& vec, const V& val)
{
    for (int i = int(vec.size()) - 1; i >= 0; --i)
        if (vec[i] == val)
            return i;

    return -1;
}

template<typename T, typename V>
inline bool contains(const T& container, const V& val)
{
    auto it = std::ranges::find(container, val);
    return (it != container.end());
}

template<typename T, typename L>
inline bool containsIf(const std::vector<T>& vec, const L& lambda)
{
    auto it = std::find_if(vec.begin(), vec.end(), lambda);
    return (it != vec.end());
}

// Helpers for STL Set ----------------------------------------------------------------------------------

template<typename T>
std::unordered_set<T>& operator+=(std::unordered_set<T>& set, const T& val)
{
    set.insert(val);
    return set;
}

template<typename T>
std::unordered_set<T>& operator+=(std::unordered_set<T>& set, const std::unordered_set<T>& appendedSet)
{
    set.insert(appendedSet.begin(), appendedSet.end());
    return set;
}

template<typename T>
std::unordered_set<T>& operator+=(std::unordered_set<T>& set, std::unordered_set<T>&& appendedSet)
{
    set.insert(std::make_move_iterator(appendedSet.begin()), std::make_move_iterator(appendedSet.end()));
    return set;
}

template<typename T>
std::unordered_set<T>& operator<<(std::unordered_set<T>& set, const T& val)
{
    set.insert(val);
    return set;
}

template<typename T>
std::unordered_set<T>& operator<<(std::unordered_set<T>& set, const std::unordered_set<T>& appendedSet)
{
    set.insert(appendedSet.begin(), appendedSet.end());
    return set;
}

template<typename T>
std::unordered_set<T>& operator<<(std::unordered_set<T>& set, std::unordered_set<T>&& appendedSet)
{
    set.insert(std::make_move_iterator(appendedSet.begin()), std::make_move_iterator(appendedSet.end()));
    return set;
}

template<typename T>
std::unordered_set<T>& operator-=(std::unordered_set<T>& set, const T& val)
{
    set.erase(val);
    return set;
}

template<typename T>
std::unordered_set<T> operator+(std::unordered_set<T>& set, const std::unordered_set<T>& appendedSet)
{
    std::unordered_set<T> result = set;
    result += appendedSet;
    return result;
}

template<typename T>
bool intersects(std::unordered_set<T>& set1, const std::unordered_set<T>& set2)
{
    std::vector<int> intersection;
    std::set_intersection(set1.begin(), set1.end(), set2.begin(), set2.end(), std::back_inserter(intersection));
    return !intersection.empty();
}

template<typename T>
std::unordered_set<T> convertQSetToSTL(const QSet<T>& set)
{
    std::unordered_set<T> result;
    result.reserve(set.size());
    for (auto&& element: set)
        result << element;
    return result;
}

template<typename T>
QSet<T> convertStlToQSet(const std::unordered_set<T>& set)
{
    QSet<T> result;
    result.reserve(set.size());
    for (auto&& element: set)
        result << element;
    return result;
}


// ------------------------------------------------------------------------------------------------------

namespace is_iterable_impl
{
    template<class T>
    using check_specs = std::void_t<std::enable_if_t<std::is_same_v<
        decltype(std::declval<T>().begin()), // has begin()
        decltype(std::declval<T>().end())    // has end()
        >>,                                 // ... begin() and end() are the same type ...
        decltype(*std::declval<T>().begin()) // ... which can be dereferenced
    >;

    template<class T, class = void>
    struct is_iterable
        : std::false_type
    {};

    template<class T>
    struct is_iterable<T, check_specs<T>>
        : std::true_type
    {};
}

template<class T>
using is_iterable = is_iterable_impl::is_iterable<T>;

template<class T>
constexpr bool is_iterable_v = is_iterable<T>::value;

namespace is_dereferenceable_impl
{
    template<class T>
    using check_specs = std::void_t<decltype(*std::declval<T>())>;

    template<class T, class = void>
    struct is_dereferenceable
        : std::false_type
    {};

    template<class T>
    struct is_dereferenceable<T, check_specs<T>>
        : std::true_type
    {};
}

template<class T>
using is_dereferenceable = is_dereferenceable_impl::is_dereferenceable<T>;

template<class T>
constexpr bool is_dereferenceable_v = is_dereferenceable<T>::value;

template<template<typename...> class T, typename... U>
T<U...> container_and(const T<U...>& A, const T<U...>& B)
{
    T<U...> result;

    for (auto&& e : A)
        if (std::find(B.begin(), B.end(), e) != B.end())
            result.insert(e);

    return result;
}

template<typename... U>
std::vector<U...> container_and(const std::vector<U...>& A, const std::vector<U...>& B)
{
    std::vector<U...> result;

    for (auto&& e : A)
        if (std::find(B.begin(), B.end(), e) != B.end())
            result << e;

    return result;
}

bool containerIntersection(const auto& A, const auto& B)
{
    for (auto&& e : A)
        if (std::find(B.begin(), B.end(), e) != B.end())
            return true;

    return false;
}

template<typename T>
// Warning: This doesn't preserve the container's order.
bool remove_single(T& v, const typename T::value_type& e)
{
    auto it = std::find(v.begin(), v.end(), e);
    if (it == v.end())
        return false;

    *it = std::move(v.back());
    v.pop_back();
    return true;
}

template<typename T, typename Pred>
// Warning: This doesn't preserve the container's order.
bool remove_single_if(T& v, Pred pred)
{
    auto it = std::find_if(v.begin(), v.end(), pred);
    if (it == v.end())
        return false;

    *it = std::move(v.back());
    v.pop_back();
    return true;
}

namespace mapbox
{
    namespace util
    {
        template <>
        struct nth<0, QVector3D> {
            inline static auto get(const QVector3D& t) {
                return t.x();
            };
        };
        template <>
        struct nth<1, QVector3D> {
            inline static auto get(const QVector3D& t) {
                return t.z();
            };
        };
    } // namespace util
} // namespace mapbox

std::string prettifyName(std::string name);

template<typename T>
inline QString toQString(const T& Var, bool prettify = true)
{
    if constexpr (std::is_same_v<T, std::string>)
        return QString::fromStdString(Var);
    else if constexpr (std::is_same_v<T, std::string_view>)
        return QString::fromStdString(std::string(Var));
    else if constexpr (std::is_same_v<T, bool>)
        return QString(Var ? "True" : "False");
    else if constexpr (std::is_same_v<T, QString>)
        return Var;
    else if constexpr (std::is_enum_v<T>)
    {
        auto name = std::string(magic_enum::enum_name(Var));
        return toQString(prettify ? prettifyName(name) : name);
    }
    else if constexpr (std::is_floating_point_v<T>)
        return QString::number(Var, 'f', 2);
    else if constexpr (std::is_arithmetic_v<T>)
        return QString::number(Var);
    else
        return QString("Specialize toQString and fromQString for ") + QString(typeid(T).name());
}

template<typename T>
inline T fromQString(const QString& Text)
{
    if constexpr (std::is_same_v<T, std::string>)
        return Text.toStdString();
    else if constexpr (std::is_same_v<T, QString>)
        return Text;
    else if constexpr (std::is_same_v<T, bool>)
    {
        auto&& L = Text.toLower();
        return (L == "true") || (L == "yes") || (L == "1");
    }
    else if constexpr (std::_Is_any_of_v<T, qint32, quint32, qint64, quint64>)
        return Text.toInt();
    else if constexpr (std::is_same_v<T, float>)
        return Text.toFloat();
    else if constexpr (std::is_same_v<T, double>)
        return Text.toDouble();
    else if constexpr (std::is_enum_v<T>)
    {
        QString outText = Text;
        std::optional<T> e = magic_enum::enum_cast<T>(outText.replace(" ", "").toStdString());
        return e ? *e : T();
    }
    else
    {
        static_assert(false, "conversion not supported");
        return T();
    }
}

template<>
inline QString toQString<QVector3D>(const QVector3D& vec, bool prettify)
{
    QString result = QString::asprintf("[%.0f, %.0f, %.0f]", vec.x(), vec.y(), vec.z());
    return result;
}

template<>
inline QString toQString<QVector4D>(const QVector4D& vec, bool prettify)
{
    QString result = QString::asprintf("[%.2f, %.2f, %.2f, %.2f]", vec.x(), vec.y(), vec.z(), vec.w());
    return result;
}

template<typename T>
void removeExpiredPointers(T& container)
{
    while (true)
    {
        auto&& it = std::find_if(container.begin(), container.end(), [](auto&& wptr) { return !wptr; });
        if (it == container.end())
            break;

        container.erase(it);
    }
}

void omnigen_assert(const QString& message);
void hideAllLayoutContents(QLayout* l);
void clearLayout(QLayout* l);

template<typename T = std::chrono::microseconds>
double timeFromLastEntrance(int ID)
{
    using namespace std::chrono;

    static QMap<int, time_point<steady_clock>> lastTimes;

    auto now = steady_clock::now();
    double result = duration_cast<T>(now - lastTimes[ID]).count();
    lastTimes[ID] = now;

    return result;
}

inline auto operator<=>(const QVector3D& A, const QVector3D& B)
{
    for (int i = 0; i < 3; ++i)
    {
        if (A[i] < B[i])
            return std::strong_ordering::less;
        if (A[i] > B[i])
            return std::strong_ordering::greater;
    }

    return std::strong_ordering::equal;
}

inline float distanceSquared(const QVector3D& A, const QVector3D& B)
{
    return (A - B).lengthSquared();
}

std::vector<QVector3D> reducePathPointsDistance(const std::vector<QVector3D>& path, float distanceBetweenPoints, bool isCircular);
void simplifieLine(std::vector<QVector3D> *line, float e);
void removeSelfIntersections(std::vector<GVector2D>* line);
std::tuple<QVector3D, QVector3D, float> distance(const std::array<QVector3D, 2>& S1, const std::array<QVector3D, 2>& S2, bool returnSquared = false);
std::tuple<GVector2D, GVector2D, float> distance(const std::array<GVector2D, 2>& S1, const std::array<GVector2D, 2>& S2, bool returnSquared = false);
std::tuple<GVector2D, float> distance(const Segment2D& segment, const GVector2D& p, bool returnSquared = false);
std::tuple<GVector2D, float> circularBoundDistance(const std::vector<GVector2D>& bounds, const GVector2D& p, bool returnSquared = false);
std::tuple<GVector2D, int, float> circularBoundDistanceAdv(const std::vector<GVector2D>& bounds, const GVector2D& p, bool returnSquared = false);
std::tuple<QVector3D, float> circularBoundDistance(const std::vector<Segment2D>& bounds, const GVector2D& p, bool returnSquared = false);
std::tuple<GVector2D, float, int> directionalBoundDistance(const std::vector<GVector2D>& bounds, const GVector2D& p, bool returnSquared = false);
std::tuple<QVector3D, float, int> directionalBoundDistance(const std::vector<QVector3D>& bounds, const QVector3D& p, bool returnSquared = false);
std::tuple<QVector3D, QVector3D, float> line2LineDistance(const std::vector<QVector3D>& l1, const std::vector<QVector3D>& l2, bool returnSquared = false, bool excludeEnds = false);
std::tuple<GVector2D, GVector2D, float> line2LineDistance(const std::vector<GVector2D>& l1, const std::vector<GVector2D>& l2, bool returnSquared = false, bool excludeEnds = false);
float distance(const GVector2D& p1, const GVector2D& p2);
float distance(const QVector3D& p1, const QVector3D& p2);
float angle360(const QVector3D& v1, const QVector3D& v2);
float angle180S(const QVector3D& v1, const QVector3D& v2);
float angle180(const QVector3D& v1, const QVector3D& v2);

constexpr float radToDeg(float angle) { return angle * 180.f / std::numbers::pi; }
constexpr float degToRad(float angle) { return angle * std::numbers::pi / 180.f; }
float angleTo180Range(float angle);
float angleTo360Range(float angle);

float distanceFromPointToInfiniteLine(const GVector2D& linePt, const GVector2D& lineDirNormalized, const GVector2D& pt, bool returnSquared = false);

enum class EIntersectionResult  
{
    None,
    Single,
    Overlap
};

EIntersectionResult lineIntersection2D(const GVector2D& v0_p0, const GVector2D& v0_p1, const GVector2D& v1_p0, const GVector2D& v1_p1);

std::optional<GVector2D> getLineIntersectionPoint(const GVector2D& pt1, const GVector2D& dir1, const GVector2D& pt2, const GVector2D& dir2);
std::optional<GVector2D> getRayCircleIntersection(const GVector2D& rayStart, const GVector2D& rayDirection, const GVector2D& center, float radius);

// Grid point
struct GPoint
{
    GPoint() = default;
    GPoint(int ix, int iz) : x(ix), z(iz) {}
    int x = 0;
    int z = 0;

    bool contains(const GVector2D& pt, bool includeEdge = true, float width = GRID_SEGMENT_WIDTH) const;
    bool isNeighbor(const GPoint& other, bool includeCorners) const;
    [[nodiscard]] GVector2D midPoint() const;
    [[nodiscard]] GPoint clamp(const GPoint& min, const GPoint& max) const;
    constexpr auto operator<=>(const GPoint&) const noexcept = default;

    [[nodiscard]] static QSet<GPoint> shaveMargin(const QSet<GPoint>& set);
    [[nodiscard]] static QSet<GPoint> growMargin(const QSet<GPoint>& set);

    GPoint operator+(const GPoint& other) const { return GPoint(x + other.x, z + other.z); };
    GPoint operator-(const GPoint& other) const { return GPoint(x - other.x, z - other.z); };
};

inline uint qHash(const GPoint& key)
{
    return qHash(QPair<int, int>(key.x, key.z));
}

// Grid 2D vector, no height
struct GVector2D
{
    constexpr GVector2D() = default;
    constexpr GVector2D(float ix, float iz) : x(ix), z(iz) {}
    GVector2D(const QVector3D& qvec3d);
    float x = 0;
    float z = 0;

    float length() const;
    float lengthSquared() const;
    float dist(const GVector2D& other, bool returnSquared = false) const;
    void normalize();
    [[nodiscard]] GVector2D normalized() const;
    bool isNull() const;
    [[nodiscard]] GVector2D rotatedRight90() const;
    [[nodiscard]] GVector2D rotatedLeft90() const;
    [[nodiscard]] GPoint floor() const;
    [[nodiscard]] GPoint ceil() const;
    [[nodiscard]] GVector2D clamp(const GVector2D& min, const GVector2D& max) const;

    float angle(const GVector2D& other) const;

	bool isInsidePolygon(const std::vector<GVector2D>& polygon) const;
    std::tuple<bool, int, float> isInsidePolygon(const CircularVectorView<std::vector, QVector3D>& polygon) const;
    std::tuple<bool, int, float> isInsidePolygon(const std::vector<QVector3D>& polygon) const;

    constexpr auto operator<=>(const GVector2D&) const noexcept = default;
    GVector2D operator+(const GVector2D& other) const noexcept;
    GVector2D& operator+=(const GVector2D& other) noexcept;
    GVector2D operator-(const GVector2D& other) const noexcept;
    GVector2D operator-() const noexcept;
    GVector2D& operator-=(const GVector2D& other) noexcept;
    GVector2D operator*(const double f) const noexcept;
    GVector2D operator*(const float f) const noexcept;
    GVector2D& operator*=(const float f) noexcept;
    GVector2D operator/(const double f) const noexcept;
    GVector2D operator/(const float f) const noexcept;
    GVector2D& operator/=(const float f) noexcept;

    static float dotProduct(const GVector2D& u, const GVector2D& v);
    static float crossProduct(const GVector2D& u, const GVector2D& v);
    static float crossProduct(const GVector2D& u, const GVector2D& v, const GVector2D& w);

    static GVector2D rotateRadians(const GVector2D& originalV, const double radians);
    static GVector2D rotateDegrees(const GVector2D& originalV, const double degrees);

    // Returns the 2D vector of this vector that faces the given other vector
    GVector2D lookAtVec2D(const GVector2D& vecToLookAt) const;

    GPoint toGPoint() const;

    constexpr operator QVector3D() const { return { x, 0, z }; }
    QVector3D to3D(float h = 0) { return { x, h, z }; }
};

GVector2D operator*(float f, const GVector2D& vec);

inline uint qHash(const GVector2D& key)
{
    return qHash(QPair<float, float>(key.x, key.z));
}

inline float distanceSquared2D(const GVector2D& A, const GVector2D& B)
{
    return (A - B).lengthSquared();
}

std::vector<double> softmax(const std::vector<double>& inValues);

template<typename T>
class hybrid_int_distribution : public std::uniform_int_distribution<T>, public std::binomial_distribution<T>
{
public:
    explicit hybrid_int_distribution(T inMin, T inMax, double flatness, double offset)
        : std::uniform_int_distribution<T>(inMin, inMax)
        , std::binomial_distribution<T>(inMax - inMin, offset)
        , flatFactor(flatness)
        , hybridMin(inMin)
        , hybridMax(inMax)
    {}

    template <class E>
    _NODISCARD T operator()(E& _Eng) 
    {
        double uniformValue = (flatFactor > 0.0f) ? std::uniform_int_distribution<T>::operator()(_Eng) : 0.0;
        double binomialValue = (flatFactor < 1.0f) ? (hybridMin + std::binomial_distribution<T>::operator()(_Eng)) : 0.0;

        return T(round(std::lerp(binomialValue, uniformValue, flatFactor)));
    }

private:
    const double flatFactor;
    const T hybridMin;
    const T hybridMax;
};

template<typename T, typename Engine>
T randomPick(const std::vector<T>& options, Engine& engine)
{
    std::uniform_int_distribution<int> dist(0, options.size() - 1);
    return options[dist(engine)];
}

struct Segment2D : QPair<GVector2D, GVector2D>
{
    using T = GVector2D;
    Segment2D() = default;
    Segment2D(const T& a, const T& b) : QPair<T, T>{ a, b } {};

    // Warning: Works only on edges aligned with axes!
    std::optional<Segment2D> operator&(const Segment2D& other) const;

    std::tuple<bool, std::pair<int, int>, float> isInsidePolygon(const std::vector<QVector3D>& polygon) const;
    bool intersects(const Segment2D& other, bool includeEnds) const;
    std::optional<GVector2D> getIntersectionPoint(const Segment2D& other) const;
    float length() const;
    float dist(const GVector2D& point) const;
    float dist(const Segment2D& segment) const;

    inline GVector2D midpoint() const { return (first + second) * 0.5f; }

    // Returns the point on this segment that is closest to the given point.
    GVector2D closestPoint(const GVector2D& inPoint) const;

    // Checks whether a given point is on this segment
    bool hasPoint(const GVector2D& inPoint) const;

private:
    static int orientation(const GVector2D& p, const GVector2D& q, const GVector2D& r);
    static bool onSegment(const GVector2D& p, const GVector2D& q, const GVector2D& r);
};

/*Compares two float values in a reliable way*/
constexpr std::strong_ordering fCmp(const float f1, const float f2, const float epsilon = std::numeric_limits<float>::epsilon())
{
    if (qAbs(f1 - f2) <= epsilon)
        return std::strong_ordering::equal;

    if (qAbs(f1 - f2) <= epsilon * qMax(qAbs(f1), qAbs(f2)))
    {
        return std::strong_ordering::equal;
    }

    if (f1 > f2)
    {
        return std::strong_ordering::greater;
    }

    return std::strong_ordering::less;
}

constexpr bool isZero(const float val)
{
    return fCmp(val, 0.f) == std::strong_ordering::equal;
}

constexpr bool vEq(const QVector3D& v1, const QVector3D& v2)
{
    return (fCmp(v1.x(), v2.x()) == 0) && (fCmp(v1.y(), v2.y()) == 0) && (fCmp(v1.z(), v2.z()) == 0);
}

constexpr bool vEq(const GVector2D& v1, const GVector2D& v2)
{
    return (fCmp(v1.x, v2.x) == std::strong_ordering::equal) && (fCmp(v1.z, v2.z) == std::strong_ordering::equal);
}

class N_Ellipse
{
public:
    N_Ellipse(const std::vector<GVector2D>& inFoci, float r);
    bool contains(const GVector2D& p);

private:
    std::vector<GVector2D> foci;
    float radius = 0.0f;
};

bool vContains(const std::vector<QVector3D>& container, const QVector3D& value);

inline const gte::Vector<2, float>& GtoV2(const GVector2D& v)
{
    return reinterpret_cast<const gte::Vector<2, float>&>(v);
}

inline const gte::Vector<2, float>& QtoV2(const QVector2D& v)
{
    return reinterpret_cast<const gte::Vector<2, float>&>(v);
}

inline const gte::Vector<3, float>& QtoV3(const QVector3D& v)
{
    return reinterpret_cast<const gte::Vector<3, float>&>(v);
}

inline const gte::Vector<4, float>& QtoV4(const QVector4D& v)
{
    return reinterpret_cast<const gte::Vector<4, float>&>(v);
}

inline const GVector2D& VtoG2(const gte::Vector<2, float>& v)
{
    return reinterpret_cast<const GVector2D&>(v);
}

inline const QVector2D& VtoQ2(const gte::Vector<2, float>& v)
{
    return reinterpret_cast<const QVector2D&>(v);
}

inline const QVector3D& VtoQ3(const gte::Vector<3, float>& v)
{
    return reinterpret_cast<const QVector3D&>(v);
}

inline const QVector4D& VtoQ4(const gte::Vector<4, float>& v)
{
    return reinterpret_cast<const QVector4D&>(v);
}

template <class T>
inline void combineHash(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template<typename SetType>
const auto& randomObjectFromSet(const SetType& set, std::mt19937& eng)
{
    std::uniform_int_distribution<int> dist(0, set.size() - 1);
    auto it = set.begin();
    std::advance(it, dist(eng));
    return *it;
}

namespace std
{
    constexpr QVector2D lerp(const QVector2D& A, const QVector2D& B, const float t)
    {
        return QVector2D(
            lerp(A.x(), B.x(), t),
            lerp(A.y(), B.y(), t)
            );
    }

    constexpr GVector2D lerp(const GVector2D& A, const GVector2D& B, const float t)
    {
        return GVector2D(lerp(A.x, B.x, t), lerp(A.z, B.z, t));
    }

    constexpr QVector3D lerp(const QVector3D& A, const QVector3D& B, const float t)
    {
        return QVector3D(
            lerp(A.x(), B.x(), t),
            lerp(A.y(), B.y(), t),
            lerp(A.z(), B.z(), t)
            );
    }

    constexpr QVector4D lerp(const QVector4D& A, const QVector4D& B, const float t)
    {
        return QVector4D(
            lerp(A.x(), B.x(), t),
            lerp(A.y(), B.y(), t),
            lerp(A.z(), B.z(), t),
            lerp(A.w(), B.w(), t)
        );
    }

    template <typename T> int sgn(T val)
    {
        return (T(0) < val) - (val < T(0));
    }

    template<> struct hash<GVector2D>
    {
        std::size_t operator()(GVector2D const& s) const noexcept
        {
            return qHash(s);
        }
    };

    template<> struct hash<GPoint>
    {
        std::size_t operator()(GPoint const& s) const noexcept
        {
            return qHash(s);
        }
    };

    template<> struct hash<QVector4D>
    {
        std::size_t operator()(QVector4D const& s) const noexcept
        {
            return static_cast<std::size_t>(qHash(QPair{ s.x(), s.y() }) + qHash(QPair{ s.z(), s.w() }));
        }
    };

    template<> struct hash<QVector3D>
    {
        std::size_t operator()(QVector3D const& s) const noexcept
        {
            return qHash(s);
        }
    };

    template<class T>
    concept Numeric = is_arithmetic_v<T>;

    template<Numeric<> I, Numeric<> J> struct hash<std::pair<I, J>>
    {
        size_t operator()(const pair<I, J>& v) const
        {
            size_t seed = 0;
            ::combineHash(seed, v.first);
            ::combineHash(seed, v.second);
            return seed;
        }
    };

}

template<typename Type>
struct std_array_traits : std::false_type { const static std::size_t size = 0; };

template<typename Item, std::size_t N>
struct std_array_traits< std::array<Item, N> > : std::true_type { const static std::size_t size = N; };

std::tuple<std::vector<QVector3D>, std::vector<uint>, std::vector<IndexType>, std::vector<IndexType>, std::vector<Segment2D>> computePerimeterForSquares(const QSet<GPoint>& squares);
QVector3D computeFaceNormal(const std::array<QVector3D, 3>& face);

double fastSin(double x);
double fastCos(double x);
double fastTan(double x);

float getRandomFloat(float min = 0.f, float max = 1.f);
int getRandomInt(int min, int max);

QSet<GVector2D> circleCircleIntersection(GVector2D p1, double r1, GVector2D p2, double r2);
float cubicInterpolation(float h0, float h1, float h2, float h3, float t);
float cubicDerivative(float h0, float h1, float h2, float h3, float t);

#define DECLARE_FLAG_OPERATORS(Flag)														        \
constexpr inline Flag operator|(const Flag& A, const Flag& B) { return Flag(int(A) | int(B));	}	\
constexpr inline Flag operator&(const Flag& A, const Flag& B)	{ return Flag(int(A) & int(B)); }	\
constexpr inline Flag operator~(const Flag& A) { return Flag(~int(A)); }							\
constexpr inline bool operator!(const Flag& A) { return int(A) == 0; }							    \
constexpr inline Flag& operator|=(Flag& A, const Flag& B) { return A = A | B; }						\
constexpr inline Flag& operator&=(Flag& A, const Flag& B) { return A = A & B; }

template<size_t N, typename T>
std::array<T, N> filled_array(const T& value)
{
    std::array<T, N> result;
    std::fill_n(result.begin(), N, value);
    return result;
}

template<typename T>
T median(std::vector<T> values)
{
    std::ranges::sort(values);
    return values[values.size() / 2];
}

template<typename T>
concept CEnum = std::is_enum_v<T>;

template<CEnum T>
std::optional<T> getOffsetedEnum(T val, int offset)
{
    constexpr int count = magic_enum::enum_count<T>();

    int intVal = static_cast<int>(val) + offset;
    if ((0 <= intVal) && (intVal < count))
        return static_cast<T>(intVal);
    
    return {};
}

template<typename CheckedPointType, typename T>
void removeSelfIntersections(std::vector<T>* inPts)
{
    auto& pts = *inPts;
    for (int i = 1; i < pts.size(); ++i)
    {
        std::array<CheckedPointType, 2> earlySegment = { pts[i - 1], pts[i] };
        for (int c = pts.size() - 1; c > i + 1; --c)
        {
            std::array<CheckedPointType, 2> lateSegment = { pts[c - 1], pts[c] };
            if (auto [p0, p1, d] = distance(earlySegment, lateSegment); d < 1.f)
            {
                // Snap to intersection point
                auto&& [s0, s1] = earlySegment;
                pts[i] = std::lerp(pts[i - 1], pts[i], distance(s0, p0) / distance(s0, s1));

                // Remove all points between i and c
                pts.erase(pts.begin() + i + 1, pts.begin() + c);

                // Done for this segment
                break;
            }
        }
    }
}

template<typename CheckedPointType, typename T>
void removeSelfIntersectionsNoSkip(std::vector<T>* inPts)
{
    auto& pts = *inPts;
    for (int i = 1; i < pts.size(); ++i)
    {
        std::array<CheckedPointType, 2> earlySegment = { pts[i - 1], pts[i] };
        for (int c = i + 2; c < pts.size(); ++c)
        {
            std::array<CheckedPointType, 2> lateSegment = { pts[c - 1], pts[c] };
            if (auto [p0, p1, d] = distance(earlySegment, lateSegment); d < 1.f)
            {
                // Snap to intersection point
                auto&& [s0, s1] = earlySegment;
                pts[i] = std::lerp(pts[i - 1], pts[i], distance(s0, p0) / distance(s0, s1));

                // Remove all points between i and c
                pts.erase(pts.begin() + i + 1, pts.begin() + c);

                // Done for this segment
                break;
            }
        }
    }
}


struct BezierCurve2D
{
public:
    BezierCurve2D() = default;
    BezierCurve2D(const std::vector<GVector2D>& pts);

    GVector2D evaluate(double t) const;

    std::vector<GVector2D> getPoints(int numSteps) const;

    float getLength(int approxPtsCount = 10) const;

private:

    std::unique_ptr<gte::BezierCurve<2, double>> curve;
};


struct BSplineCurve
{
    BSplineCurve() = default;
    BSplineCurve(const std::vector<QVector3D>& pts, int degree = 3);

    QVector3D evaluate(float t) const;
    std::vector<QVector3D> getPoints(int numSteps) const;

private:

    std::vector<gte::Vector3<float>> samplePts;
    std::unique_ptr<gte::BSplineCurveFit<float>> spline;
};

template<typename ComputeType, typename PointType>
std::tuple<ComputeType, float> circularBoundDistance(const CircularVectorView<std::vector, PointType>& cPts, const ComputeType& p, bool returnSquared = false)
{
	if (cPts.getSize() == 0)
		return { ComputeType(), -1.0f };

	float minD = std::numeric_limits<float>::max();
	std::vector<ComputeType> nearest;

	for (int i = 0; i < cPts.getSize(); ++i)
	{
		int i2 = cPts.findIdx(i, 1);

        ComputeType v1;
        float d;
        if constexpr (std::is_same_v<PointType, ComputeType>)
        {
            std::tie(v1, d) = distance({ cPts[i], cPts[i2] }, p, true);
        }
        else
        {
            std::tie(v1, d) = distance({ ComputeType(cPts[i]), ComputeType(cPts[i2]) }, p, true);
        }

		if (d < minD)
		{
			minD = d;
			nearest = { v1 };
		}
		else if (d == minD)
		{
			nearest << v1;
		}
	}

	std::sort(nearest.begin(), nearest.end());

	return { nearest.front(), returnSquared ? minD : sqrt(minD) };
}

template<typename ComputeType, typename PointType>
std::tuple<ComputeType, int, float> circularBoundDistanceAdv(const CircularVectorView<std::vector, PointType>& cPts, const PointType& p, bool returnSquared = false)
{
	if (cPts.getSize() == 0)
		return { ComputeType(), -1, -1.0f };

	struct PointWithIndex
	{
        ComputeType p;
		int i;
	};

	float minD = std::numeric_limits<float>::max();
	std::vector<PointWithIndex> nearest;

	for (int i = 0; i < cPts.getSize(); ++i)
	{
		int i2 = cPts.findIdx(i, 1);

		ComputeType v1;
		float d;
		if constexpr (std::is_same_v<PointType, ComputeType>)
		{
			std::tie(v1, d) = distance({ cPts[i], cPts[i2] }, p, true);
		}
		else
		{
			std::tie(v1, d) = distance({ ComputeType(cPts[i]), ComputeType(cPts[i2]) }, p, true);
		}

		if (d < minD)
		{
			minD = d;
			nearest = { PointWithIndex(v1, i) };
		}
		else if (d == minD)
		{
			nearest <<= PointWithIndex(v1, i);
		}
	}

	std::sort(nearest.begin(), nearest.end(), [](auto&& A, auto&& B) { return A.i < B.i; });

	return { nearest.front().p, nearest.front().i, returnSquared ? minD : sqrt(minD) };
}

template<typename T, typename... Ts>
void forAll(const auto& lambda)
{
    lambda.template operator() < T > ();
    if constexpr (sizeof...(Ts) > 0)
        forAll<Ts...>(lambda);
}

template<typename T>
struct Singleton
{
    static T& get()
    {
        static T instance;
        return instance;
    }
};
