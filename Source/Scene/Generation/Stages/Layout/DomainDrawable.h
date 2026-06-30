#pragma once
#include "Scene/OmnigenDrawable.h"
#include "Data/DomainData.h"
#include <set>
#include "Utils/OmniBin/OmniBinQt.h"
#include <QEnableSharedFromThis>

class OmnigenPropertyListBase;
class DDomainHandle;

class DDomain : public OmnigenDrawable, public QEnableSharedFromThis<DDomain>
{
    static inline ShaderPipeline shaderPipeline;

public:
    DDomain() = default;

    static const QMap<EDomainType, QVector4D> Colors;

    // Drawable
    virtual ERenderPriority getRenderPriority() const override { return ERenderPriority::Domain; };
    virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
    virtual void bindShader(const OmnigenCamera& camera) override;
    virtual void cacheBoundingBox() override;
    virtual void draw() override;
    virtual void unbindShader() override;
    virtual bool shouldDraw(int vIdx) const override;
    virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

    const auto& getType() const { return type; }
    const auto& getName() const { return data->name; }
    const auto& getSquares() const { return squares; }
    const auto& getHandle() const { return handle; }
    const auto& getPerimeter() const { return perimeter; }
    BoundingBox getBoundingRectangle() const;

    bool isPointInDomain(const GVector2D& inPoint) const;

    template<EDomainType DT = EDomainType(-1)>
    auto getData() const 
    { 
        if constexpr (DT == EDomainType(-1))
            return data;
        else
            return data.staticCast<DomainData<DT>>();
    }

    void setType(EDomainType newType);
    void setName(const QString& newName);
    void setData(QSharedPointer<DomainDataBase> newData);
    void setSquares(const QSet<GPoint>& newSquares);
    void bindHandle(QSharedPointer<DDomainHandle> dh);

protected:
    virtual void createShader() override;
    virtual void createDefaultLodLevel() override;
    void createData();
    void update();

    // The ball that the user can interact with to work with this domain.
    QWeakPointer<DDomainHandle> handle;

    // Rendering
    std::vector<GLuint> wireframeIndices;
    std::vector<GLuint> boundsIndices;

    // Set of grid coordinates. These mark the [lowerX, lowerZ] corner in grid units.
    // To convert grid units to world units, multiply by GRID_SEGMENT_WIDTH.
    QSet<GPoint> squares;

    // Terrain, Biome, Water
    EDomainType type = EDomainType::Last;

    // Additional data set in Properties Section
    QSharedPointer<DomainDataBase> data;

    // Optimized 2D edges bounding this domain's squares.
    std::vector<Segment2D> perimeter;

    friend class DDomainHandle;
    FRIEND_OMNIBIN(DDomain);
};

namespace EAC
{
    struct SaveDomainData
    {
        template<EDomainType DT>
        static void Action(const QSharedPointer<DomainDataBase>* data, OmniBin<std::ios::out>& omniBin)
        {
            omniBin << reinterpret_cast<const QSharedPointer<DomainData<DT>>&>(*data);
        }
    };

    struct LoadDomainData
    {
        template<EDomainType DT>
        static void Action(QSharedPointer<DomainDataBase>* data, OmniBin<std::ios::in>& omniBin)
        {
            omniBin >> reinterpret_cast<QSharedPointer<DomainData<DT>>&>(*data);
        }
    };

    struct SaveDomainDataToHistory
    {
        template<EDomainType DT>
        static auto Action(const QSharedPointer<DomainDataBase>& data, int keyIdx)
        {
            DomainData<DT>& hData = static_cast<DomainData<DT>&>(*data);
            History::GetContext()->ParentContext()->Present()->Save("Custom_DomainData" + std::to_string(keyIdx), hData);
        }
    };

    struct LoadDomainDataFromHistory
    {
        template<EDomainType DT>
        static std::optional<QSharedPointer<DomainDataBase>> Action(int keyIdx)
        {
            auto hData = QSharedPointer<DomainData<DT>>::create();
            if (History::GetContext()->ParentContext()->Present()->Load("Custom_DomainData" + std::to_string(keyIdx), *hData))
                return hData;
            else
                return {};
        }
    };
}

inline void omniSave(const DDomain& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.guid;
    omniBin << object.type;
    omniBin << object.squares;
    
    EDomainTypeConstexpr::UseIn<EAC::SaveDomainData>(object.type, &object.data, omniBin);
}

inline void omniLoad(DDomain& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.guid;
    omniBin >> object.type;
    omniBin >> object.squares;

    EDomainTypeConstexpr::UseIn<EAC::LoadDomainData>(object.type, &object.data, omniBin);

    object.initialize();
    object.setSquares(object.squares);
}