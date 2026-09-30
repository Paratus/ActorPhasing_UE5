// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseKey.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "ActorPhaseSubsystem.generated.h"

class AActor;
class AActorPhaseVolume;
class APawn;
class APlayerController;
class APlayerState;
class UActorPhaseComponent;
class UActorPhaseMembershipComponent;
class UNetConnection;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActorPhaseUnviewedSignature, const FActorPhaseKey&, PhaseKey);

/**
 * Native change events consumed by the replication adapters (Replication Graph node, Iris group
 * filter). Registration is reported as a change from no keys, unregistration as a change to no keys.
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FActorPhaseKeysChangedNative, AActor* /*Actor*/, const TArray<FActorPhaseKey>& /*OldKeys*/, const TArray<FActorPhaseKey>& /*NewKeys*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FActorPhaseMembershipChangedNative, UActorPhaseMembershipComponent* /*Membership*/, const TArray<FActorPhaseKey>& /*OldMembership*/, const TArray<FActorPhaseKey>& /*NewMembership*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FActorPhaseMembershipEventNative, UActorPhaseMembershipComponent* /*Membership*/);

/**
 * Server-side bookkeeping for Actor Phasing (also present on clients, where it only serves lookups).
 *
 * - Registry of phased actors and viewer memberships for O(1) relevancy lookups.
 * - Closes an actor's channel the moment it stops being relevant to a viewer, so a phase switch
 *   is immediate instead of waiting for the engine's RelevantTimeout.
 * - Collision scopes: pawns inside a scope stop colliding with pawns and actors of other phases
 *   (movement sweeps only; queries are filtered by the consumer with the library).
 * - Idle rules: fires OnPhaseUnviewed once a phase has had no viewer (member or observer) for a
 *   grace period, so the consumer can despawn that phase's actors.
 */
UCLASS()
class ACTORPHASING_API UActorPhaseSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem of the world the object lives in, or null. */
	static UActorPhaseSubsystem* Get(const UObject* WorldContextObject);

	// ~USubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	// ~FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override;

	// ---- Registry (called by the components) ----

	void RegisterPhaseComponent(UActorPhaseComponent* Component);
	void UnregisterPhaseComponent(UActorPhaseComponent* Component);
	void NotifyPhaseKeysChanged(UActorPhaseComponent* Component, const TArray<FActorPhaseKey>& OldKeys);

	void RegisterMembership(UActorPhaseMembershipComponent* Component);
	void UnregisterMembership(UActorPhaseMembershipComponent* Component);
	void NotifyMembershipChanged(UActorPhaseMembershipComponent* Component, const TArray<FActorPhaseKey>& OldMembership);
	void NotifyObservedPhasesChanged(UActorPhaseMembershipComponent* Component, const TArray<FActorPhaseKey>& OldObservedPhases);
	/** Re-keys the viewer cache after a possession or controller change. */
	void RefreshViewerBindings(UActorPhaseMembershipComponent* Component);

	void RegisterVolume(AActorPhaseVolume* Volume);
	void UnregisterVolume(AActorPhaseVolume* Volume);

	// ---- Native events (replication adapters) ----

	/** An actor's phase keys changed. Fired on registration ({} -> keys), key changes and unregistration (keys -> {}). */
	FActorPhaseKeysChangedNative OnActorPhaseKeysChangedNative;

	/** A registered viewer's membership set changed. */
	FActorPhaseMembershipChangedNative OnMembershipChangedNative;

	/** A registered viewer's observed phases changed. */
	FActorPhaseMembershipChangedNative OnObservedPhasesChangedNative;

	/** A membership component registered with this world. */
	FActorPhaseMembershipEventNative OnMembershipRegisteredNative;

	/** A membership component unregistered from this world. */
	FActorPhaseMembershipEventNative OnMembershipUnregisteredNative;

	/** A membership was re-bound to its controller and pawn (possession or PlayerState change). */
	FActorPhaseMembershipEventNative OnViewerBindingsRefreshedNative;

	// ---- Hot-path lookups ----

	/** The actor's registered phase component, or null. */
	UActorPhaseComponent* FindPhaseComponent(const AActor* Actor) const;

	/** The viewer's membership (controller, PlayerState or pawn), or null for the base phase. Cached. */
	UActorPhaseMembershipComponent* FindMembership(const AActor* Viewer) const;

	// ---- Queries ----

	/** Number of viewers whose membership contains the phase. */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	int32 GetViewerCount(const FActorPhaseKey& Key) const;

	/** Number of viewers observing the phase without being members. */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Observation")
	int32 GetObserverCount(const FActorPhaseKey& Key) const;

	/** Number of registered actors carrying the phase. */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing")
	int32 GetActorCount(const FActorPhaseKey& Key) const;

	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void GetActorsInPhase(const FActorPhaseKey& Key, TArray<AActor*>& OutActors) const;

	UFUNCTION(BlueprintCallable, Category = "Actor Phasing")
	void GetViewersInPhase(const FActorPhaseKey& Key, TArray<APlayerState*>& OutViewers) const;

	/** Visits every registered phased actor with its current keys (replication adapters seed themselves with this). */
	void ForEachPhasedActor(TFunctionRef<void(AActor* Actor, const TArray<FActorPhaseKey>& Keys)> Visitor) const;

	/** Visits every registered membership component. */
	void ForEachMembership(TFunctionRef<void(UActorPhaseMembershipComponent* Membership)> Visitor) const;

	// ---- Collision scopes ----

	/** Adds a pawn to a scope; it stops colliding with scope members of other phases. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Collision")
	void RegisterPawnInCollisionScope(UObject* Scope, APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Collision")
	void UnregisterPawnFromCollisionScope(UObject* Scope, APawn* Pawn);

	/** Adds a (usually static) phased actor to a scope. UActorPhaseComponent::SetCollisionScope does this for you. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Collision")
	void RegisterActorInCollisionScope(UObject* Scope, AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Collision")
	void UnregisterActorFromCollisionScope(UObject* Scope, AActor* Actor);

	/** Recomputes every move-ignore pair inside the scope. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Collision")
	void RecomputeCollisionScope(UObject* Scope);

	// ---- Channels ----

	/** Closes the actor's channel on every connection it is no longer relevant to. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Replication")
	void RefreshActorChannels(AActor* Actor);

	/** Closes the channels of every registered phased actor the viewer can no longer see. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Replication")
	void RefreshViewerChannels(AActor* Viewer);

	// ---- Idle rules ----

	/**
	 * Fires OnPhaseUnviewed once the phase has had no viewer for GraceSeconds (negative = project
	 * default). The rule is consumed when it fires; register again after respawning.
	 */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Lifecycle")
	void RegisterIdleRule(const FActorPhaseKey& Key, float GraceSeconds = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Lifecycle")
	void ClearIdleRule(const FActorPhaseKey& Key);

	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Lifecycle")
	bool HasIdleRule(const FActorPhaseKey& Key) const;

	/** A phase with an idle rule has had no viewer for its grace period. */
	UPROPERTY(BlueprintAssignable, Category = "Actor Phasing|Lifecycle")
	FActorPhaseUnviewedSignature OnPhaseUnviewed;

	// ---- Diagnostics ----

	/** Multi-line summary of phases, viewers, actors, scopes and idle rules. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Debug")
	FString DescribeState() const;

	/** True on dedicated/listen servers and in standalone; false on pure clients. */
	bool IsServerLike() const;

private:
	struct FViewerCacheEntry
	{
		TWeakObjectPtr<UActorPhaseMembershipComponent> Membership;
		/** Pinned entries come from RegisterMembership and are never re-resolved. */
		bool bPinned = false;
		double NextRecheckTime = 0.0;
	};

	struct FCollisionScope
	{
		TArray<TWeakObjectPtr<APawn>> Pawns;
		TArray<TWeakObjectPtr<AActor>> Actors;
	};

	struct FIdleRule
	{
		float GraceSeconds = 0.0f;
		/** Seconds accumulated without a viewer; negative while the phase has viewers. */
		float UnviewedFor = -1.0f;
	};

	void AddViewerKeys(const TArray<FActorPhaseKey>& Keys, int32 Delta);
	void AddObserverKeys(const TArray<FActorPhaseKey>& Keys, int32 Delta);
	void PinViewer(const AActor* Viewer, UActorPhaseMembershipComponent* Component);
	void UnpinViewer(const AActor* Viewer);
	APlayerController* ResolveViewerController(const AActor* Viewer) const;
	UNetConnection* FindConnectionFor(const APlayerController* Controller) const;
	void RecomputeScopesContaining(const APawn* Pawn);
	void ClearPawnIgnores(APawn* Pawn, const FCollisionScope& Scope) const;
	void PollIdleRules(float DeltaTime);
	void DrawDebug() const;

	TMap<TObjectKey<AActor>, TWeakObjectPtr<UActorPhaseComponent>> PhaseComponentsByActor;
	TMap<FActorPhaseKey, TSet<TWeakObjectPtr<AActor>>> ActorsByKey;

	TSet<TWeakObjectPtr<UActorPhaseMembershipComponent>> Memberships;
	TMap<TObjectKey<UActorPhaseMembershipComponent>, TArray<TObjectKey<AActor>>> PinnedViewersByMembership;
	TMap<FActorPhaseKey, int32> ViewerCountByKey;
	TMap<FActorPhaseKey, int32> ObserverCountByKey;
	mutable TMap<TObjectKey<AActor>, FViewerCacheEntry> ViewerCache;

	TMap<TObjectKey<UObject>, FCollisionScope> Scopes;
	TMap<FActorPhaseKey, FIdleRule> IdleRules;
	TArray<TWeakObjectPtr<AActorPhaseVolume>> Volumes;
};
