// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhasingDebug.h"

#include "ActorPhaseSubsystem.h"
#include "ActorPhasingModule.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace ActorPhasingDebug
{
	static TAutoConsoleVariable<int32> CVarDraw(
		TEXT("ActorPhasing.Debug.Draw"),
		0,
		TEXT("1 = draw phase volumes and per-pawn membership labels (servers and standalone only)."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarLog(
		TEXT("ActorPhasing.Debug.Log"),
		0,
		TEXT("1 = log every membership and phase-key change."),
		ECVF_Cheat);

	static FAutoConsoleCommandWithWorld CmdDump(
		TEXT("ActorPhasing.Dump"),
		TEXT("Print phases, viewers, actors, collision scopes and idle rules of the current world."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (const UActorPhaseSubsystem* Subsystem = World ? World->GetSubsystem<UActorPhaseSubsystem>() : nullptr)
			{
				UE_LOG(LogActorPhasing, Log, TEXT("%s"), *Subsystem->DescribeState());
			}
			else
			{
				UE_LOG(LogActorPhasing, Log, TEXT("No Actor Phasing subsystem in this world."));
			}
		}));

	bool IsDrawEnabled()
	{
		return CVarDraw.GetValueOnGameThread() != 0;
	}

	bool IsVerboseLogEnabled()
	{
		return CVarLog.GetValueOnGameThread() != 0;
	}
}
