// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseKey.h"
#include "Components/ActorComponent.h"
#include "ActorPhaseMembershipComponent.generated.h"

class APawn;
class APlayerController;
class APlayerState;
class UActorPhaseMembershipComponent;
class UActorPhaseSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FActorPhaseMembershipChangedSignature, UActorPhaseMembershipComponent*, Component, const TArray<FActorPhaseKey>&, OldMembership, const TArray<FActorPhaseKey>&, NewMembership);

/**
 * The set of phases a player can see. Attach to the PlayerState (recommended: it survives pawn
 * changes) or to the PlayerController.
 *
 * - Membership defaults to {Base}. SetMembership REPLACES the set, so a player moved into phase X
 *   no longer sees base-phase players, and base-phase players no longer see them (symmetric).
 * - The possessed pawn's phase component mirrors this membership automatically, so the pawn is
 *   visible exactly to the viewers that share a phase with it.
 * - Replicated to the owning client only, for UI. Servers decide membership.
 *
 * Observed phases are a second, independent set: the viewer also sees the world content of those
 * phases (actors whose phase component is visible to observers) without joining them. Observation
 * never reaches the pawn, so players stay visible only to members of their phase, and observers
 * cannot interact with what they observe (UActorPhasingLibrary::CanViewerInteractWithActor).
 * Use it to show a phase's props from a distance before the viewer walks into it.
 */
UCLASS(ClassGroup = (ActorPhasing), meta = (BlueprintSpawnableComponent, DisplayName = "Actor Phase Membership"))
class ACTORPHASING_API UActorPhaseMembershipComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UActorPhaseMembershipComponent();

	/** Replaces the membership. Invalid keys are dropped; an empty set falls back to {Base}. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing")
	void SetMembership(const TArray<FActorPhaseKey>& NewMembership);

	/** Replaces the membership with exactly one phase. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing")
	void SetSinglePhase(const FActorPhaseKey& Key);

	/** Returns the viewer to the shared base phase. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing")
	void ResetToBase();

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	TArray<FActorPhaseKey> GetMembership() const { return Membership; }

	/** Allocation-free accessor for hot paths. */
	const TArray<FActorPhaseKey>& GetMembershipRef() const { return Membership; }

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	bool HasPhase(const FActorPhaseKey& Key) const;

	/** True when the membership is exactly {Base}. */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	bool IsInBasePhase() const;

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	bool IntersectsKeys(const TArray<FActorPhaseKey>& Keys) const;

	/** Replaces the observed phases (world content visible without joining). Invalid keys are dropped. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing|Observation")
	void SetObservedPhases(const TArray<FActorPhaseKey>& NewObservedPhases);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing|Observation")
	void AddObservedPhase(const FActorPhaseKey& Key);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing|Observation")
	void RemoveObservedPhase(const FActorPhaseKey& Key);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Actor Phasing|Observation")
	void ClearObservedPhases();

	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Observation")
	TArray<FActorPhaseKey> GetObservedPhases() const { return ObservedPhases; }

	/** Allocation-free accessor for hot paths. */
	const TArray<FActorPhaseKey>& GetObservedPhasesRef() const { return ObservedPhases; }

	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Observation")
	bool IsObservingPhase(const FActorPhaseKey& Key) const { return ObservedPhases.Contains(Key); }

	/** The PlayerState this membership belongs to (owner, or the controller's PlayerState). */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	APlayerState* GetOwningPlayerState() const;

	/** The controller that views through this membership, when known. */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	APlayerController* GetOwningPlayerController() const;

	/** The pawn currently mirroring this membership, when possessed. */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	APawn* GetViewedPawn() const;

	/** Fired on the server and on the owning client whenever the membership changes. */
	UPROPERTY(BlueprintAssignable, Category = "Actor Phasing")
	FActorPhaseMembershipChangedSignature OnMembershipChanged;

	/** Fired on the server and on the owning client whenever the observed phases change. */
	UPROPERTY(BlueprintAssignable, Category = "Actor Phasing|Observation")
	FActorPhaseMembershipChangedSignature OnObservedPhasesChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(ReplicatedUsing = OnRep_Membership)
	TArray<FActorPhaseKey> Membership;

	UFUNCTION()
	void OnRep_Membership(const TArray<FActorPhaseKey>& OldMembership);

	UPROPERTY(ReplicatedUsing = OnRep_ObservedPhases)
	TArray<FActorPhaseKey> ObservedPhases;

	UFUNCTION()
	void OnRep_ObservedPhases(const TArray<FActorPhaseKey>& OldObservedPhases);

	UFUNCTION()
	void HandlePlayerStatePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);

	UFUNCTION()
	void HandleControllerPawnChanged(APawn* OldPawn, APawn* NewPawn);

private:
	void ApplyMembership(TArray<FActorPhaseKey> Sanitized);
	void ApplyObservedPhases(TArray<FActorPhaseKey> Sanitized);
	void PushMembershipToPawn(APawn* Pawn);
	void BindPawnEvents();
	void UnbindPawnEvents();
	UActorPhaseSubsystem* GetPhaseSubsystem() const;
	bool IsServerLike() const;

	static void SanitizeMembership(TArray<FActorPhaseKey>& Keys);

	bool bPawnEventsBound = false;
	bool bRegistered = false;
};
