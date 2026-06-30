#pragma once
#include <QVBoxLayout>
#include "OmnigenPropertyList.h"
#include "Editable.h"

class OmnigenDrawable;

class QOmnigenPropertiesSection : public QWidget
{
    Q_OBJECT

public:
    QOmnigenPropertiesSection(QWidget* parent);

    void set(QSharedPointer<OmnigenPropertyListBase> props);
    void clear();

    std::optional<uint64_t> getPropertyOwner() const;

private:
    void updateListForObject(QSharedPointer<Editable> object, bool reset);
    void updateList();
    void createFieldWidget(int fieldIdx);

    QVBoxLayout* mainLayout = nullptr;
    QGridLayout* fieldLayout = nullptr;
    QWidget* innerContent = nullptr;
    std::optional<QSharedPointer<OmnigenPropertyListBase>> currentList;

    friend class OmnigenPropertyListBase;
};