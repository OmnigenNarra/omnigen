#pragma once

//#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
//#include "Modules/ModuleManager.h"

class FOmnigenAssetModule : public IModuleInterface
{
public:

	//~ IModuleInterface interface

	virtual void StartupModule() override { }
	virtual void ShutdownModule() override { }

	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}
};

IMPLEMENT_MODULE(FOmnigenAssetModule, OmnigenAsset);