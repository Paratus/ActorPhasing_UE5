// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseKey.h"
#include "ReplicationGraph.h"
#include "UObject/ObjectKey.h"
#include "ActorPhasingReplicationGraphNode.generated.h"

class UActorPhaseSubsystem;
class UReplicationGraphNode_ActorList;
class UReplicationGraphNode_GridSpatialization2D;

/**
 * Replication Graph node that gathers phased actors only for the connections whose membership
 * intersects their keys.
 *
 * The node keeps one child node per phase key. An actor is inserted into the child of every key it
 * carries; a connection gathers the children of every key in its viewers' memberships. Actors that
 * are visible to observers live in a second set of children, which connections also gather for the
 * phases their viewers only observe (UActorPhaseMembershipComponent::SetObservedPhases). Children are
 * 2D spatialization grids by default (bSpatializePhases), so a large phase is still distance
 * culled; with spatialization off, or for bAlwaysRelevant actors, they are plain actor lists.
 *
 * Route only phased actors here (see UActorPhasingReplicationGraph::ShouldRouteToPhaseNode). An
 * unphased actor that is routed here anyway lands in an "unphased" list gathered for everyone. Key
 * changes are followed through UActorPhaseSubsystem::OnActorPhaseKeysChangedNative; when an actor
 * loses all keys it moves to the unphased list until the graph re-routes it.
 *
 * Use it as a global node created with UReplicationGraph::CreateNewNode so that
 * PrepareForReplication reaches the child grids. Effective keys are resolved when the actor is
 * added (owner/attach inheritance included); call NotifyActorPhaseKeysChanged if an inherited
 * phase changes without the actor's own component changing.
 */
UCLASS(transient)
class ACTORPHASINGREPLICATIONGRAPH_API UActorPhasingReplicationGraphNode : public UReplicationGraphNode
{
	GENERATED_BODY()

public:
	UActorPhasingReplicationGraphNode();

	//~ Begin UReplicationGraphNode
	virtual void NotifyAddNetworkActor(const FNewReplicatedActorInfo& ActorInfo) override;
	virtual bool NotifyRemoveNetworkActor(const FNewReplicatedActorInfo& ActorInfo, bool bWarnIfNotFound = true) override;
	virtual bool NotifyActorRenamed(const FRenamedReplicatedActorInfo& ActorInfo, bool bWarnIfNotFound = true) override;
	virtual void NotifyResetAllNetworkActors() override;
	virtual void PrepareForReplication() override;
	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;
	virtual void TearDown() override;
	virtual void LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const override;
	virtual void GetAllActorsInNode_Debugging(TArray<FActorRepListType>& OutArray) const override;
	//~ End UReplicationGraphNode

	/** Children are spatialization grids (true) or actor lists (false). Set before the first actor is added. */
	UPROPERTY()
	bool bSpatializePhases = true;

	/** Cell size of the per-phase grids. */
	UPROPERTY()
	float CellSize = 10000.0f;

	/** Spatial bias of the per-phase grids. */
	UPROPERTY()
	FVector2D SpatialBias = FVector2D(-UE_OLD_WORLD_MAX, -UE_OLD_WORLD_MAX);

	/** True while the actor is held by this node. */
	bool IsTracking(const AActor* Actor) const { return TrackedActors.Contains(Actor); }

	/** Keys the node currently files the actor under (empty when unphased or not tracked). */
	TArray<FActorPhaseKey> GetTrackedKeys(const AActor* Actor) const;

	/** Number of distinct phase keys that currently have a child node. */
	int32 GetPhaseCount() const;

	/** True when the node files the actor as visible to observers of its phases. */
	bool IsTrackedAsObservable(const AActor* Actor) const;

	/** Number of actors held by the node. */
	int32 GetTrackedActorCount() const { return TrackedActors.Num(); }

	/**
	 * Re-files an actor under new keys. Called automatically from the subsystem's key-change
	 * event; call it yourself when an inherited phase changes.
	 */
	void NotifyActorPhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& NewKeys);

	/** Same, with the observer visibility supplied instead of resolved from the actor. */
	void NotifyActorPhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& NewKeys, bool bVisibleToObservers);

	/** Collects the phase keys a connection may see, deduplicated across its viewers. */
	void CollectViewerKeys(const FConnectionGatherActorListParameters& Params, TArray<FActorPhaseKey>& OutKeys) const;

	/** Collects the phases a connection's viewers observe without being members of them. */
	void CollectObservedKeys(const FConnectionGatherActorListParameters& Params, const TArray<FActorPhaseKey>& MemberKeys, TArray<FActorPhaseKey>& OutKeys) const;

private:
	struct FPhaseChild
	{
		/** Spatialized actors (only with bSpatializePhases). */
		TObjectPtr<UReplicationGraphNode_GridSpatialization2D> Grid;
		/** Non-spatialized actors: everything without spatialization, bAlwaysRelevant actors otherwise. */
		TObjectPtr<UReplicationGraphNode_ActorList> List;
		int32 ActorCount = 0;
	};

	struct FTrackedActor
	{
		FName StreamingLevelName;
		TArray<FActorPhaseKey> Keys;
		/** The actor was filed in lists (true) or grids (false). */
		bool bInLists = false;
		/** The actor was filed in the observable children. */
		bool bVisibleToObservers = false;
	};

	TMap<FActorPhaseKey, FPhaseChild>& ChildrenFor(bool bVisibleToObservers) { return bVisibleToObservers ? ObservableChildrenByKey : ChildrenByKey; }
	FPhaseChild& FindOrAddChild(const FActorPhaseKey& Key, bool bVisibleToObservers);
	void ReleaseChildIfEmpty(const FActorPhaseKey& Key, bool bVisibleToObservers);
	bool UsesListsFor(const AActor* Actor) const;
	void FileActor(const FNewReplicatedActorInfo& ActorInfo, const TArray<FActorPhaseKey>& Keys, bool bVisibleToObservers, FTrackedActor& Tracked);
	void UnfileActor(const FNewReplicatedActorInfo& ActorInfo, const FTrackedActor& Tracked);
	FGlobalActorReplicationInfo* FindGlobalInfo(AActor* Actor) const;

	void EnsureBound();
	void Unbind();
	void HandlePhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& OldKeys, const TArray<FActorPhaseKey>& NewKeys);

	/** Strong references live in AllChildNodes. Actors hidden from observers (players, etc.). */
	TMap<FActorPhaseKey, FPhaseChild> ChildrenByKey;

	/** Actors visible to observers of their phases (world content). */
	TMap<FActorPhaseKey, FPhaseChild> ObservableChildrenByKey;

	/** Actors routed here without a phase: gathered for every connection. */
	TObjectPtr<UReplicationGraphNode_ActorList> UnphasedList;

	TMap<TObjectKey<AActor>, FTrackedActor> TrackedActors;

	TWeakObjectPtr<UActorPhaseSubsystem> BoundSubsystem;
	FDelegateHandle KeysChangedHandle;

	/** Reused per gather to avoid allocations. */
	TArray<FActorPhaseKey> ViewerKeysScratch;
	TArray<FActorPhaseKey> ObservedKeysScratch;
};
