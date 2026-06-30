#pragma once
#include "Fields/OmnigenField.h"

namespace Design
{
    class SelectionBase;
}

// Base class and interface for all property set implementations.
// If the object is modifiable outside the property system, id must represent a OmnigenDrawable's id (TODO: Object class?)
// May be associated with a selection object.
class OmnigenPropertyListBase
{
public:
    // If proplist is made for an OmnigenDrawable, both @id and @inSelection must be provided.
    OmnigenPropertyListBase(qint64 id, const QSharedPointer<Design::SelectionBase>& inSelection = nullptr);

    // Retrieve currently active property list
    static std::optional<QSharedPointer<OmnigenPropertyListBase>> getCurrent();

    virtual ~OmnigenPropertyListBase() = default;

    const auto& getFields() const { return fields; }
    const auto& getControlSection() const { return controlSection; }
    void addField(QSharedPointer<OmnigenFieldBase> field) { fields << field; };
    void setControlSection(QWidget* w) { controlSection = w; }
    void update();

    // Id of the object whose fields are being displayed.
    qint64 ownerId;

    // Selection object linked with this property list
    QSharedPointer<Design::SelectionBase> selection;

    // Cosmetic switch, some lists like it set to true
    bool isWide = false;

protected:
    std::vector<QSharedPointer<OmnigenFieldBase>> fields;

    // Optional completely custom widget.
    QWidget* controlSection = nullptr;
};
