// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/** Console-variable backed debug switches (ActorPhasing.Debug.*). */
namespace ActorPhasingDebug
{
	/** ActorPhasing.Debug.Draw: draw volumes and per-pawn membership labels on servers and in standalone. */
	bool IsDrawEnabled();

	/** ActorPhasing.Debug.Log: log every membership and key change at Log verbosity. */
	bool IsVerboseLogEnabled();
}
