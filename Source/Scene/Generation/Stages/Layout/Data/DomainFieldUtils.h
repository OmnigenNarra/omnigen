#pragma once
#include <QSharedPointer>
#include "Editor/StageTools/Layout/LayoutSelection.h"
#include "Editor/Sections/PropertySystem/Fields/ComboBoxField.h"

class DDomain;
class DomainDataBase;

namespace detail
{
    QSharedPointer<DomainDataBase> getDataFromDomain(QSharedPointer<DDomain> domain);
}

template<typename T, typename L1, typename L2>
auto createDomainSetFieldLambda(L1 GetDomainLambda, L2 SetLambda)
{
    return [GetDomainLambda, SetLambda](auto&& newValue)
    {
        auto domain = GetDomainLambda();
        emit Editable::aboutToBeModified(domain);

        // If Set invalidates the field, end here.
        if (!SetLambda(detail::getDataFromDomain(domain).staticCast<T>(), newValue))
            return false;

        // Ensure manipulated domain is selected and viewed.
        if (History::GetContext()->IsUndoingOrRedoing())
            Design::LayoutSelectionMgr::get()->setSelection<Design::ELayoutSelection::Domain>({ domain->getHandle() });

        emit Editable::modified(domain);

        return true;
    };
}

template<typename T, typename L1, typename L2>
auto createDomainSetMultiFieldLambda(L1 GetDomainLambda, L2 SetLambda)
{
    return [GetDomainLambda, SetLambda](auto&& key, auto&& newValue, EContainerAction ca)
    {
        auto domain = GetDomainLambda();
        emit Editable::aboutToBeModified(domain);

        // If Set invalidates the field, end here.
        if (!SetLambda(detail::getDataFromDomain(domain).staticCast<T>(), key, newValue, ca))
            return false;

        // Ensure manipulated domain is selected and viewed.
        if (History::GetContext()->IsUndoingOrRedoing())
            Design::LayoutSelectionMgr::get()->setSelection<Design::ELayoutSelection::Domain>({ domain->getHandle() });

        emit Editable::modified(domain);

        return true;
    };
}