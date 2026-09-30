// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

using UnrealBuildTool;

public class ActorPhasing : ModuleRules
{
	public ActorPhasing(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DeveloperSettings"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"NetCore"
			}
		);
	}
}
