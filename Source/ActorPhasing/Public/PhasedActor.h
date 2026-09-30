// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseInterfaces.h"
#include "GameFramework/Actor.h"
#include "PhasedActor.generated.h"

class UActorPhaseComponent;

/**
 * Convenience base for replicated actors that live in phases: carries a phase component and
 * consults the phasing rule from IsNetRelevantFor.
 *
 * Existing actor hierarchies do not need this class. Add a UActorPhaseComponent and copy the
 * IsNetRelevantFor override below (phase rule first, then Super).
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (ActorPhasing))
class ACTORPHASING_API APhasedActor : public AActor, public IPhasedActorInterface
{
	GENERATED_BODY()

public:
	APhasedActor();

	// ~IPhasedActorInterface
	virtual UActorPhaseComponent* GetActorPhaseComponent() const override { return PhaseComponent; }

	// ~AActor
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	UActorPhaseComponent* GetPhaseComponent() const { return PhaseComponent; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Actor Phasing")
	TObjectPtr<UActorPhaseComponent> PhaseComponent;
};
