// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseInterfaces.h"
#include "ActorPhaseKey.h"
#include "GameFramework/Actor.h"
#include "UObject/ScriptInterface.h"
#include "ActorPhaseVolume.generated.h"

class AActorPhaseVolume;
class APawn;
class APlayerState;
class UBoxComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FActorPhaseVolumePawnSignature, AActorPhaseVolume*, Volume, APlayerState*, PlayerState, APawn*, Pawn);

/**
 * A level-placed box that moves players into a phase while they stand inside it and returns them
 * to the base phase when they leave.
 *
 * The phase is decided per viewer by ResolvePhaseForViewer: override it in a subclass or
 * Blueprint, assign a Resolver object, or use FixedPhase. Occupants are also registered as a
 * collision scope so pawns of different phases stop colliding inside the volume.
 *
 * Collision setup follows the trigger idiom: WorldDynamic object type, query only, ignores every
 * channel except Pawn (overlap), so it never answers world-geometry queries.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (ActorPhasing), HideCategories = (Rendering, Physics, LOD, Cooking, HLOD, Input, Networking))
class ACTORPHASING_API AActorPhaseVolume : public AActor
{
	GENERATED_BODY()

public:
	AActorPhaseVolume();

	/**
	 * Decides the phase for a viewer entering (or re-evaluated inside) this volume.
	 * Default order: Resolver object, then FixedPhase, else false (stay in the base phase).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Actor Phasing")
	bool ResolvePhaseForViewer(APlayerState* Viewer, FActorPhaseKey& OutKey) const;

	/** Re-runs the resolution for a pawn that is inside (use after the consumer's selection changed). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing")
	void ReapplyForPawn(APawn* Pawn);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing")
	void ReapplyForPlayerState(APlayerState* PlayerState);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing")
	void ReapplyAll();

	/** Uses a fixed phase for every viewer (bEnable=false clears it). Re-applies to current occupants. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing")
	void SetFixedPhase(const FActorPhaseKey& Key, bool bEnable = true);

	/** Assigns the resolver object consulted before the fixed phase. Re-applies to current occupants. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing")
	void SetResolver(TScriptInterface<IActorPhaseResolverInterface> InResolver);

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	UBoxComponent* GetVolumeBounds() const { return VolumeBounds; }

	/** World-space bounds of the box. */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	FBox GetWorldBounds() const;

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	bool ContainsPoint(const FVector& Point) const;

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	bool IsPawnInside(const APawn* Pawn) const;

	/** PlayerStates whose pawn is currently inside and applied. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void GetOccupants(TArray<APlayerState*>& OutOccupants) const;

	/** Server: a pawn with a PlayerState entered and its phase was resolved (applied or left at base). */
	UPROPERTY(BlueprintAssignable, Category = "Actor Phasing")
	FActorPhaseVolumePawnSignature OnPawnEntered;

	/** Server: a pawn left (or was destroyed inside) and was returned to the base phase. */
	UPROPERTY(BlueprintAssignable, Category = "Actor Phasing")
	FActorPhaseVolumePawnSignature OnPawnExited;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Actor Phasing")
	TObjectPtr<UBoxComponent> VolumeBounds;

	/** Optional object implementing ActorPhaseResolverInterface; consulted before FixedPhase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Phasing")
	TScriptInterface<IActorPhaseResolverInterface> Resolver;

	/** When set, every viewer inside the volume enters FixedPhase (unless a Resolver decided first). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Phasing")
	bool bUseFixedPhase = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Phasing", meta = (EditCondition = "bUseFixedPhase"))
	FActorPhaseKey FixedPhase;

	/** Return leaving viewers to the base phase. Disable for one-way transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Phasing")
	bool bResetToBaseOnExit = true;

	/** Register occupants as a collision scope (pawns of different phases stop colliding). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Phasing")
	bool bMaintainCollisionScope = true;

	/** A pawn that spawns inside the volume may not have a PlayerState yet; retry this many times. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Phasing", meta = (ClampMin = "0"))
	int32 PlayerStateRetryAttempts = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Phasing", meta = (ClampMin = "0.1", Units = "Seconds"))
	float PlayerStateRetrySeconds = 2.0f;

private:
	void TryApplyPhase(TWeakObjectPtr<APawn> Pawn, int32 AttemptsLeft);
	void ApplyPhaseToPawn(APawn* Pawn, APlayerState* PlayerState);
	void RemovePawn(APawn* Pawn);

	TSet<TWeakObjectPtr<APawn>> Occupants;
};
