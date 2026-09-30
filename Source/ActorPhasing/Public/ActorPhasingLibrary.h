// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseKey.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ActorPhasingLibrary.generated.h"

class AActor;
class UActorPhaseComponent;
class UActorPhaseMembershipComponent;
class UActorPhaseSubsystem;

/**
 * The phasing rules. Everything here is stateless; the world subsystem only provides fast lookups.
 *
 * Rule: an actor with no valid phase key is in every phase. Otherwise a viewer sees it when the
 * viewer's membership intersects the actor's keys, or when the viewer owns it. A viewer with no
 * membership component is in the base phase.
 *
 * Observation: a viewer also sees an actor whose keys intersect the viewer's OBSERVED phases when
 * the actor's phase component is visible to observers (world content, never a player's pawn).
 * Observation grants sight only; interaction still requires membership.
 */
UCLASS()
class ACTORPHASING_API UActorPhasingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The relevancy rule. Call from AActor::IsNetRelevantFor with the RealViewer it receives (the
	 * connection's PlayerController); a PlayerState or pawn also works as the viewer.
	 * Returns true for unphased actors, for the viewer's own actors, and for observed world content.
	 */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing", meta = (DefaultToSelf = "Actor"))
	static bool IsActorRelevantToViewer(const AActor* Actor, const AActor* RealViewer);

	/**
	 * The relevancy rule without observation: true only when the viewer is a MEMBER of one of the
	 * actor's phases (or the actor is unphased or owned by the viewer).
	 */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing", meta = (DefaultToSelf = "Actor"))
	static bool IsActorInViewerPhase(const AActor* Actor, const AActor* Viewer);

	/** True when the two actors share a phase, or either one is unphased. */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	static bool AreActorsInCompatiblePhases(const AActor* A, const AActor* B);

	/**
	 * Interaction / targeting gate: the viewer (controller, PlayerState or pawn) can reach the target.
	 * Membership only: observed content is visible but cannot be interacted with.
	 */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	static bool CanViewerInteractWithActor(const AActor* Viewer, const AActor* Target);

	/**
	 * The keys that decide an actor's visibility: its own phase component, else an owner's
	 * (up to the configured owner-chain depth), else its attach parent's.
	 * @return false when the actor is in every phase (OutKeys is left empty).
	 */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	static bool GetEffectivePhaseKeys(const AActor* Actor, TArray<FActorPhaseKey>& OutKeys);

	/**
	 * GetEffectivePhaseKeys plus whether observers of those phases can see the actor (decided by
	 * the phase component the keys come from).
	 */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Observation")
	static bool GetEffectivePhaseInfo(const AActor* Actor, TArray<FActorPhaseKey>& OutKeys, bool& bOutVisibleToObservers);

	/** The viewer's membership. @return false when the viewer has no membership component (base phase). */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	static bool GetViewerMembership(const AActor* Viewer, TArray<FActorPhaseKey>& OutMembership);

	/** Finds the membership component for a controller, PlayerState or pawn (cached by the subsystem). */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	static UActorPhaseMembershipComponent* FindMembershipForViewer(const AActor* Viewer);

	/** Finds the actor's own phase component (registry first, then component search). */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	static UActorPhaseComponent* FindPhaseComponent(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	static bool KeysIntersect(const TArray<FActorPhaseKey>& A, const TArray<FActorPhaseKey>& B);

	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Keys")
	static FActorPhaseKey MakePhaseKey(FName Domain, int64 Id);

	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Keys")
	static FActorPhaseKey BasePhaseKey();

	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Keys")
	static bool IsPhaseKeyValid(const FActorPhaseKey& Key);

	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Keys")
	static bool IsBasePhaseKey(const FActorPhaseKey& Key);

	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Keys", meta = (DisplayName = "Equal (Phase Key)", CompactNodeTitle = "=="))
	static bool PhaseKeysEqual(const FActorPhaseKey& A, const FActorPhaseKey& B);

	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Keys", meta = (DisplayName = "To String (Phase Key)", CompactNodeTitle = "->", BlueprintAutocast))
	static FString PhaseKeyToString(const FActorPhaseKey& Key);

	/** The membership every viewer without a component is treated as having: {Base}. */
	static const TArray<FActorPhaseKey>& GetBaseMembership();

	/**
	 * Allocation-free form of GetEffectivePhaseKeys for hot paths. The returned pointer aliases a
	 * live component's array and must not be stored.
	 */
	static const TArray<FActorPhaseKey>* ResolveEffectivePhaseKeys(const AActor* Actor, const UActorPhaseSubsystem* Subsystem);

	/** The phase component whose keys decide the actor's visibility (see GetEffectivePhaseKeys), or null. */
	static const UActorPhaseComponent* ResolveEffectivePhaseComponent(const AActor* Actor, const UActorPhaseSubsystem* Subsystem);

	/** True when the viewer owns the actor (its own pawn, controller, PlayerState or owned actors). */
	static bool IsActorOwnedByViewer(const AActor* Actor, const AActor* Viewer);

private:
	static bool EvaluateRelevancy(const AActor* Actor, const AActor* Viewer, bool bIncludeObservation);
};
