// Copyright Epic Games, Inc. All Rights Reserved.

#include "OmnigenCommands.h"

#define LOCTEXT_NAMESPACE "FOmnigenModule"

void FOmnigenCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "Omnigen", "Execute Omnigen action", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
