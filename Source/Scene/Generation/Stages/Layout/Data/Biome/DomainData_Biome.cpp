#include "stdafx.h"
#include "DomainData_Biome.h"

#include "../DomainFieldUtils.h"
#include "Editor/Sections/PropertySystem/Fields/ComboBoxField.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"

void DomainData<EDomainType::Biome>::fillProps(QSharedPointer<OmnigenPropertyListBase> props)
{
    DomainDataBase::fillProps(props);

    qint64 id = props->ownerId;

    auto temperatureOptions = gatherEnumsForComboField<ETemperature>();

    props->addField(QSharedPointer<TOmnigenField<ETemperature, ComboFieldEdit<ETemperature>>>::create("Temperature",
        [this]() { return temperature; },
        createDomainSetFieldLambda<DomainData>(
            [id]() { return getOwner(id); },
            [id](auto owner, auto&& value) { owner->temperature = value; owner->name = owner->makeName(); return true; }),
        [temperatureOptions]() { return new ComboFieldEdit(temperatureOptions); }
    ));

    auto humidityOptions = gatherEnumsForComboField<EHumidity>();

    props->addField(QSharedPointer<TOmnigenField<EHumidity, ComboFieldEdit<EHumidity>>>::create("Humidity",
        [this]() { return humidity; },
        createDomainSetFieldLambda<DomainData>(
            [id]() { return getOwner(id); },
            [id](auto owner, auto&& value) { owner->humidity = value; owner->name = owner->makeName(); return true; }),
        [humidityOptions]() { return new ComboFieldEdit(humidityOptions); }
    ));

    auto maxLayerOptions = gatherEnumsForComboField<EBiomeLayer>();

    props->addField(QSharedPointer<TOmnigenField<EBiomeLayer, ComboFieldEdit<EBiomeLayer>>>::create("Highest allowed layer",
        [this]() { return maxLayer; },
        createDomainSetFieldLambda<DomainData>(
            [id]() { return getOwner(id); },
            [id](auto owner, auto&& value) { owner->maxLayer = value; return true; }),
        [maxLayerOptions]() { return new ComboFieldEdit(maxLayerOptions); }
    ));

    props->addField(QSharedPointer<TOmnigenField<float>>::create("Vegetation density",
        [this]() { return foliageDensity; },
        createDomainSetFieldLambda<DomainData>(
            [id]() { return getOwner(id); },
            [id](auto owner, auto&& value) { owner->foliageDensity = value; return true; })
    ));
}

QString DomainData<EDomainType::Biome>::makeName(bool isInitial /*= false*/)
{
    auto domains = Generation::Data::get()->getAllDomains<EDomainType::Biome>();

    int domainCount = isInitial ? 1 : 0;
    for (auto&& [handle, domain] : domains)
        if (domain->getData<EDomainType::Biome>()->temperature == temperature && domain->getData<EDomainType::Biome>()->humidity == humidity)
            domainCount++;

    QString name = QString::fromStdString(std::string(magic_enum::enum_name(temperature)));
    name += " " + QString::fromStdString(std::string(magic_enum::enum_name(humidity)));
    name += " Biome";
    name += " " + QString::number(domainCount);

    return name;
}