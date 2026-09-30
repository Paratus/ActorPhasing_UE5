// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"

ACTORPHASING_API DECLARE_LOG_CATEGORY_EXTERN(LogActorPhasing, Log, All);

/** Runtime module for the Actor Phasing plugin. */
class FActorPhasingModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
