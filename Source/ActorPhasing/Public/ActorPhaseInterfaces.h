// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseKey.h"
#include "UObject/Interface.h"
#include "ActorPhaseInterfaces.generated.h"

class APlayerState;
class UActorPhaseComponent;
class UActorPhaseMembershipComponent;

/** Optional: an actor that exposes its phase component directly (skips the registry lookup). */
UINTERFACE(BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class ACTORPHASING_API UPhasedActorInterface : public UInterface
{
	GENERATED_BODY()
};

class ACTORPHASING_API IPhasedActorInterface
{
	GENERATED_BODY()

public:
	/** The phase component that decides this actor's phases. May return null (all phases). */
	virtual UActorPhaseComponent* GetActorPhaseComponent() const = 0;
};

/** Optional: a viewer actor (PlayerController or PlayerState) that exposes its membership directly. */
UINTERFACE(BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class ACTORPHASING_API UActorPhaseViewerInterface : public UInterface
{
	GENERATED_BODY()
};

class ACTORPHASING_API IActorPhaseViewerInterface
{
	GENERATED_BODY()

public:
	/** The membership component of this viewer. May return null (base phase). */
	virtual UActorPhaseMembershipComponent* GetActorPhaseMembership() const = 0;
};

/**
 * Decides which phase a viewer enters when a phase volume admits it.
 * Implementable in C++ or Blueprint; assign the implementing object to AActorPhaseVolume::Resolver.
 */
UINTERFACE(BlueprintType, Blueprintable)
class ACTORPHASING_API UActorPhaseResolverInterface : public UInterface
{
	GENERATED_BODY()
};

class ACTORPHASING_API IActorPhaseResolverInterface
{
	GENERATED_BODY()

public:
	/**
	 * Resolves the phase a viewer should occupy inside a volume.
	 * @param Viewer   The PlayerState entering the volume.
	 * @param Volume   The volume asking (an AActorPhaseVolume).
	 * @param OutKey   The resolved phase key.
	 * @return true when OutKey should be applied; false keeps the viewer in the base phase.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Actor Phasing")
	bool ResolvePhaseForViewer(APlayerState* Viewer, AActor* Volume, FActorPhaseKey& OutKey);
};
