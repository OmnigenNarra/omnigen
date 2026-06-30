#include "stdafx.h"
#include "DomainFieldUtils.h"
#include "../DomainDrawable.h"
#include "Editable.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"

QSharedPointer<DomainDataBase> detail::getDataFromDomain(QSharedPointer<DDomain> domain)
{
    return domain->getData();
}