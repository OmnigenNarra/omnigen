#include "stdafx.h"
#include "OmnigenPropertiesSection.h"
#include "Omnigen.h"
#include "Editor/StageTools/SelectionMgrBase.h"

#include <QLabel>

QOmnigenPropertiesSection::QOmnigenPropertiesSection(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout();

    parent->setLayout(layout);
    innerContent = new QWidget(this);
    innerContent->setStyleSheet("padding: 4ex;");
    layout->addWidget(innerContent);

    mainLayout = new QVBoxLayout(innerContent);
    mainLayout->setSpacing(5);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    layout->addStretch(100);

    auto connectForever = Editable::EventMgr.AddEventListener<EEditableEvents::Modified>(this, &QOmnigenPropertiesSection::updateListForObject);
}

void QOmnigenPropertiesSection::set(QSharedPointer<OmnigenPropertyListBase> props)
{
    if (props)
    {
        currentList = props;
        updateList();
    }
    else
    {
        clear();
    }
}

void QOmnigenPropertiesSection::clear()
{
    currentList.reset();
    updateList();
}

std::optional<uint64_t> QOmnigenPropertiesSection::getPropertyOwner() const
{
    if (currentList)
        return currentList.value()->ownerId;

    return {};
}

void QOmnigenPropertiesSection::updateListForObject(QSharedPointer<Editable> object, bool reset)
{
    auto&& drawable = object.dynamicCast<OmnigenDrawable>();
    if (!drawable)
        return;

    if (!currentList || (*currentList)->ownerId != drawable->getGuid())
        return;

    if (!reset)
        updateList();
    else
        set((*currentList)->selection->makePropertyList());
}

void QOmnigenPropertiesSection::updateList()
{
    // Clear old contents
    clearLayout(mainLayout);

    if (!currentList)
        return;

    if (auto* controlSection = (*currentList)->getControlSection(); controlSection)
        mainLayout->addWidget(controlSection);

    auto policyToUse = (*currentList)->isWide ? QSizePolicy::Expanding : QSizePolicy::Fixed;
 
    QWidget* fieldHost = new QWidget();
    fieldHost->setSizePolicy(policyToUse, policyToUse);
    mainLayout->addWidget(fieldHost);

    fieldLayout = new QGridLayout(fieldHost);
    fieldLayout->setSpacing(5);
    fieldLayout->setContentsMargins(0, 0, 0, 0);

    // Create property list
    for (int i=0; i<(*currentList)->getFields().size(); ++i)
        createFieldWidget(i);

    adjustSize();
}

void QOmnigenPropertiesSection::createFieldWidget(int fieldIdx)
{
    auto&& field = (*currentList)->getFields()[fieldIdx];

    // Label
    auto* label = field->createFieldLabelWidget();
    label->setStyleSheet("padding: 1ex;");
    fieldLayout->addWidget(label, fieldIdx, 0);
    label->show();

    // Input area
    auto* input = field->createFieldInputWidget();
    field->update();
    input->setStyleSheet("padding: 1ex;");
    fieldLayout->addWidget(input, fieldIdx, 1);
    input->show();
}
