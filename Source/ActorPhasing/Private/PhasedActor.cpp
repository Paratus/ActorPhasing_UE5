// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "PhasedActor.h"

#include "ActorPhaseComponent.h"
#include "ActorPhasingLibrary.h"

APhasedActor::APhasedActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	// Phased actors must stay awake: a dormant actor is never re-evaluated for relevancy.
	NetDormancy = DORM_Awake;

	PhaseComponent = CreateDefaultSubobject<UActorPhaseComponent>(TEXT("PhaseComponent"));
}

bool APhasedActor::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	if (!UActorPhasingLibrary::IsActorRelevantToViewer(this, RealViewer))
	{
		return false;
	}
	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}
