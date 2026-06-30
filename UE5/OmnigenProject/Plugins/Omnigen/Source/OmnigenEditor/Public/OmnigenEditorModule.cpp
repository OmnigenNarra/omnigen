// Copyright Epic Games, Inc. All Rights Reserved.

#include "OmnigenEditorModule.h"

#include "OmnigenStyle.h"
#include "OmnigenCommands.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "OmnigenImporter.h"


static const FName OmnigenTabName("Omnigen");

#define LOCTEXT_NAMESPACE "FOmnigenEditorModule"

void FOmnigenEditorModule::StartupModule()
{

}

void FOmnigenEditorModule::ShutdownModule()
{

}



#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FOmnigenEditorModule, OmnigenEditor)