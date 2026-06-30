#include "stdafx.h"
#include "OmnigenPropertyList.h"
#include "Omnigen.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"


OmnigenPropertyListBase::OmnigenPropertyListBase(qint64 id, const QSharedPointer<Design::SelectionBase>& inSelection)
    : ownerId(id)
    , selection(inSelection)
{
}

std::optional<QSharedPointer<OmnigenPropertyListBase>> OmnigenPropertyListBase::getCurrent()
{
    return Omnigen::get()->getProperties()->currentList;
}

void OmnigenPropertyListBase::update()
{
    for (auto&& field : fields)
        field->update();
}
