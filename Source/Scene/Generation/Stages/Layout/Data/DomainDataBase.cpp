#include "stdafx.h"
#include "DomainDataBase.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Omnigen.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"
#include "DomainFieldUtils.h"
#include "Editor/Sections/PropertySystem/Fields/ConstField.h"

void DomainDataBase::fillProps(QSharedPointer<OmnigenPropertyListBase> props)
{
    qint64 id = props->ownerId;

    if (Generation::Data::get()->getGenerationStage() == EGenerationStage::Layout)
        props->addField(QSharedPointer<TOmnigenField<QString>>::create(
            "Name",
            [id]()
            {
                return getOwner(id)->getName();
            },
            createDomainSetFieldLambda<DomainDataBase>(
                [id]() { return getOwner(id); },
                [](auto data, auto&& newName)
                {
                    data->name = newName;
                    return true;
                })));
    else if (Generation::Data::get()->getGenerationStage() == EGenerationStage::Ridges)
        props->addField(QSharedPointer<TOmnigenField<QString, ConstField<QString>>>::create(
            "Name",
            [id]()
            {
                return getOwner(id)->getName();
            },
            createDomainSetFieldLambda<DomainDataBase>(
                [id]() { return getOwner(id); },
                [](auto data, auto&& newName)
                {
                    data->name = newName;
                    return true;
                })));
}

QSharedPointer<DDomain> DomainDataBase::getOwner(qint64 id)
{
    auto domain_it = Generation::Data::get()->findDomainByGuid(id);
    if (!domain_it)
        OmniLog(ELoggingLevel::Critical) << "Failed to find domain: " <<= id;

    auto&& test = (*domain_it)->getName();
    return *domain_it;
}

QString DomainDataBase::makeName(bool isInitial /*= false*/)
{
    static int nameCounter = 0;
    return "domain" + QString::number(++nameCounter);
}
