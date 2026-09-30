// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseKey.h"
#include "Components/ActorComponent.h"
#include "ActorPhaseComponent.generated.h"

class UActorPhaseComponent;
class UActorPhaseSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FActorPhaseKeysChangedSignature, UActorPhaseComponent*, Component, const TArray<FActorPhaseKey>&, OldKeys, const TArray<FActorPhaseKey>&, NewKeys);

/**
 * Places its owning actor in one or more phases.
 *
 * Server-authoritative and not replicated: clients never need it because an actor they cannot see
 * is never replicated to them. With no valid key the owner is in every phase ("unphased").
 * A pawn's component normally mirrors its owner's membership (see UActorPhaseMembershipComponent).
 *
 * Actors that carry this component must also consult UActorPhasingLibrary::IsActorRelevantToViewer
 * from their AActor::IsNetRelevantFor override (or derive from APhasedActor).
 */
UCLASS(ClassGroup = (ActorPhasing), meta = (BlueprintSpawnableComponent, DisplayName = "Actor Phase"))
class ACTORPHASING_API UActorPhaseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UActorPhaseComponent();

	/** Replaces the key set. Invalid keys are dropped and duplicates removed. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void SetPhaseKeys(const TArray<FActorPhaseKey>& NewKeys);

	/** Replaces the key set with exactly one key. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void SetSinglePhase(const FActorPhaseKey& Key);

	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void AddPhaseKey(const FActorPhaseKey& Key);

	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void RemovePhaseKey(const FActorPhaseKey& Key);

	/** Removes every key: the owner becomes visible in every phase. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void ClearPhaseKeys();

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	TArray<FActorPhaseKey> GetPhaseKeys() const { return PhaseKeys; }

	/** Allocation-free accessor for hot paths. */
	const TArray<FActorPhaseKey>& GetPhaseKeysRef() const { return PhaseKeys; }

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	bool HasPhaseKey(const FActorPhaseKey& Key) const;

	/** True when at least one valid key is set (the owner is NOT in every phase). */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	bool IsPhased() const;

	/**
	 * Optional collision scope (usually the phase volume the owner lives in). Pawns registered in
	 * the same scope stop colliding with this actor when their phases do not intersect.
	 */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void SetCollisionScope(UObject* Scope);

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	UObject* GetCollisionScope() const { return CollisionScope.Get(); }

	/** True when the keys follow the owning player's membership (set automatically on pawns). */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	bool MirrorsOwnerMembership() const { return bMirrorsOwnerMembership; }

	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void SetMirrorsOwnerMembership(bool bInMirrors);

	/**
	 * True when viewers that only OBSERVE one of this actor's phases can see it (see
	 * UActorPhaseMembershipComponent::SetObservedPhases). Always false while the keys mirror a
	 * player's membership: players are visible to members of their phase only.
	 */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	bool IsVisibleToObservers() const { return bVisibleToObservers && !bMirrorsOwnerMembership; }

	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void SetVisibleToObservers(bool bInVisible);

	/** Fired on the server whenever the key set changes. */
	UPROPERTY(BlueprintAssignable, Category = "Actor Phasing")
	FActorPhaseKeysChangedSignature OnPhaseKeysChanged;

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnUnregister() override;

	/** Initial keys. Leave empty for an actor that is visible in every phase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actor Phasing")
	TArray<FActorPhaseKey> PhaseKeys;

	/** Keys follow the owning player's membership. Set automatically for possessed pawns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actor Phasing")
	bool bMirrorsOwnerMembership = false;

	/**
	 * Observers of this actor's phases can see it without joining the phase (world content such as
	 * props and scenery). Turn off for content that must stay hidden until the viewer joins.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actor Phasing")
	bool bVisibleToObservers = true;

private:
	void ApplyKeys(TArray<FActorPhaseKey> Sanitized);
	/** Re-announces the unchanged keys after an observability change so adapters re-file the actor. */
	void NotifyObservabilityChanged();
	void TryRegister();
	void TryUnregister();
	UActorPhaseSubsystem* GetPhaseSubsystem() const;

	static void SanitizeKeys(TArray<FActorPhaseKey>& Keys);

	TWeakObjectPtr<UObject> CollisionScope;
	bool bRegistered = false;
};
