// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class OmnigenAsset : ModuleRules
{
	public OmnigenAsset(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		//bEnforceIWYU = true;
		//bLegacyPublicIncludePaths = false;
		//bUseUnity = false;

		/*PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);*/


		PrivateIncludePaths.AddRange(new string[] { Path.Combine(ModuleDirectory, "Private") });
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Foliage"
			}
			);
			
		
		/*PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);*/
	}
}
