#include "stdafx.h"
#include "DomainData_Water.h"
#include "../DomainFieldUtils.h"
#include "Editor/Sections/PropertySystem/Fields/ComboBoxField.h"

void DomainData<EDomainType::Water>::fillProps(QSharedPointer<OmnigenPropertyListBase> props)
{
    DomainDataBase::fillProps(props);
    qint64 id = props->ownerId;

    props->addField(QSharedPointer<TOmnigenField<EIslandsCoverage, ComboFieldEdit<EIslandsCoverage>>>::create("Seaside land coverage",
        [this]() { return landCoverage; },
        createDomainSetFieldLambda<DomainData>(
            [id]() { return getOwner(id); },
            [](auto owner, auto&& value) { owner->landCoverage = value; return true; }
            ),
        []() { return new ComboFieldEdit(gatherEnumsForComboField<EIslandsCoverage>()); }
    ));

    props->addField(QSharedPointer<TOmnigenField<EAmountOfIslands, ComboFieldEdit<EAmountOfIslands>>>::create("Amount of small islands",
        [this]() { return amountOfSmallIslands; },
        createDomainSetFieldLambda<DomainData>(
            [id]() { return getOwner(id); },
            [](auto owner, auto&& value) { owner->amountOfSmallIslands = value; return true; }
            ),
        []() { return new ComboFieldEdit(gatherEnumsForComboField<EAmountOfIslands>()); }
    ));

    props->addField(QSharedPointer<TOmnigenField<EAmountOfIslands, ComboFieldEdit<EAmountOfIslands>>>::create("Amount of medium islands",
        [this]() { return amountOfMediumIslands; },
        createDomainSetFieldLambda<DomainData>(
            [id]() { return getOwner(id); },
            [](auto owner, auto&& value) { owner->amountOfMediumIslands = value; return true; }
            ),
        []() { return new ComboFieldEdit(gatherEnumsForComboField<EAmountOfIslands>()); }
    ));

    props->addField(QSharedPointer<TOmnigenField<EAmountOfIslands, ComboFieldEdit<EAmountOfIslands>>>::create("Amount of large islands",
        [this]() { return amountOfLargeIslands; },
        createDomainSetFieldLambda<DomainData>(
            [id]() { return getOwner(id); },
            [](auto owner, auto&& value) { owner->amountOfLargeIslands = value; return true; }
            ),
        []() { return new ComboFieldEdit(gatherEnumsForComboField<EAmountOfIslands>()); }
    ));

    props->addField(QSharedPointer<TOmnigenField<EShorelineComplexity, ComboFieldEdit<EShorelineComplexity>>>::create("Complexity of shoreline path",
        [this]() { return shorelineComplexity; },
        createDomainSetFieldLambda<DomainData>(
            [id]() { return getOwner(id); },
            [](auto owner, auto&& value) { owner->shorelineComplexity = value; return true; }
            ),
        []() { return new ComboFieldEdit(gatherEnumsForComboField<EShorelineComplexity>()); }
    ));
}

QString DomainData<EDomainType::Water>::makeName(bool isInitial /*= false*/)
{
    auto domains = Generation::Data::get()->getAllDomains<EDomainType::Water>();

    int domainCount = domains.size() + 1;

    QString name = "Water ";
    name += " " + QString::number(domainCount);

    return name;
}