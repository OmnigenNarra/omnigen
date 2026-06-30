// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class OmnigenEditor : ModuleRules
{
	public OmnigenEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		//bEnforceIWYU = true;
		//bLegacyPublicIncludePaths = false;
		bUseUnity = false;

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			); 
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
				Path.Combine(ModuleDirectory, "Public")
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
				Path.Combine(ModuleDirectory, "Private")
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"ProceduralMeshComponent",
				"StaticMeshDescription",
				"MeshDescription",
				"CoreUObject",
				"Engine",
				"EditorScriptingUtilities",
				"MaterialEditor",
				"OmnigenAsset",
				"AssetTools",
				"TextureCompressor",
				"Foliage",
				"UnrealEd"

				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
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
				"OmnigenAsset",
				"Foliage",
				"Water"
				// ... add private dependencies that you statically link with here ...	
			}
			);

		CppStandard = CppStandardVersion.Cpp17;
	}
}
