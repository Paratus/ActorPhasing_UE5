// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

using UnrealBuildTool;

/// <summary>
/// Optional adapter that makes Actor Phasing work under the Replication Graph replication driver.
/// The core module stays free of this dependency; projects on the standard driver do not need it.
/// </summary>
public class ActorPhasingReplicationGraph : ModuleRules
{
	public ActorPhasingReplicationGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ActorPhasing",
				"ReplicationGraph"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"NetCore"
			}
		);

		SetupGameplayDebuggerSupport(Target);
	}
}
