// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "OmnigenStyle.h"

class FOmnigenCommands : public TCommands<FOmnigenCommands>
{
public:

	FOmnigenCommands()
		: TCommands<FOmnigenCommands>(TEXT("Omnigen"), NSLOCTEXT("Contexts", "Omnigen", "Omnigen Plugin"), NAME_None, FOmnigenStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
