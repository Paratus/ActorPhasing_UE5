// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

using UnrealBuildTool;

/// <summary>
/// Optional adapter that makes Actor Phasing work under the Iris replication system. It maps every
/// distinct phase key set to an Iris exclusion group and keeps the per-connection group status in
/// sync with viewer memberships. The core module stays free of this dependency.
/// </summary>
public class ActorPhasingIris : ModuleRules
{
	public ActorPhasingIris(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ActorPhasing"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"NetCore"
			}
		);

		// Adds IrisCore and defines UE_WITH_IRIS.
		SetupIrisSupport(Target);
	}
}
