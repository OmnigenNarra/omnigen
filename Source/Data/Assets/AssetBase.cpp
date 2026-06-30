#include "stdafx.h"
#include "Assets.h"
#include "Omnigen.h"
#include "Editor/Sections/AssetMgr/AssetMgrSection.h"
#include "Editor/Sections/PropertySystem/OmnigenPropertiesSection.h"

OmnigenAssetBase::OmnigenAssetBase()
    : id(makeGuid())
{
}

QString OmnigenAssetBase::getUniqueName(const QString& name)
{
    QString baseName = !name.isEmpty() ? name : "Asset";
    QString newName = baseName;
    newName.replace(' ', '_');

    int counter = 0;
    while (true)
    {
        bool exists = false;
        for (auto&& [type, assets] : QOmnigenAssetMgrSection::getAssets())
            for(auto&& [id, asset] : assets)
                if (asset && asset->name == newName)
                {
                    exists = true;
                    break;
                }

        if (!exists)
            return newName;

        newName = baseName + "_" + QString::number(counter++);
    }

}

void omniSave(const OmnigenAssetBase& object, OmniBin<std::ios::out>& omniBin)
{
    omniBin << object.name;
    omniBin << object.id;
}

void omniLoad(OmnigenAssetBase& object, OmniBin<std::ios::in>& omniBin)
{
    omniBin >> object.name;
    omniBin >> object.id;
}