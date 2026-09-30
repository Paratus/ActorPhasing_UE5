// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseKey.h"
#include "ReplicationGraph.h"
#include "UObject/ObjectKey.h"
#include "ActorPhasingReplicationGraph.generated.h"

class UActorPhaseSubsystem;
class UActorPhasingReplicationGraphNode;
class UReplicationGraphNode_ActorList;
class UReplicationGraphNode_AlwaysRelevant_ForConnection;
class UReplicationGraphNode_GridSpatialization2D;
#if WITH_GAMEPLAY_DEBUGGER
class AGameplayDebuggerCategoryReplicator;
#endif

USTRUCT()
struct FActorPhasingConnectionNodePair
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UNetConnection> NetConnection = nullptr;

	UPROPERTY()
	TObjectPtr<UReplicationGraphNode_AlwaysRelevant_ForConnection> Node = nullptr;
};

/**
 * Drop-in Replication Graph with phasing: the engine's basic graph (2D grid, always-relevant list,
 * per-connection always-relevant node, owner-only actors) plus a UActorPhasingReplicationGraphNode
 * that receives every phased actor.
 *
 * Enable it in DefaultEngine.ini:
 *
 *   [/Script/OnlineSubsystemUtils.IpNetDriver]
 *   ReplicationDriverClassName="/Script/ActorPhasingReplicationGraph.ActorPhasingReplicationGraph"
 *
 * Projects that already own a graph should instead create the phase node in their own
 * InitGlobalGraphNodes, route actors with ShouldRouteToPhaseNode, and bind
 * UActorPhaseSubsystem::OnActorPhaseKeysChangedNative to re-route actors that gain or lose a phase
 * (see HandlePhaseKeysChanged for the pattern).
 */
UCLASS(transient, config = Engine)
class ACTORPHASINGREPLICATIONGRAPH_API UActorPhasingReplicationGraph : public UReplicationGraph
{
	GENERATED_BODY()

public:
	UActorPhasingReplicationGraph();

	//~ Begin UReplicationGraph
	virtual void InitGlobalActorClassSettings() override;
	virtual void InitGlobalGraphNodes() override;
	virtual void InitConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection) override;
	virtual void RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo) override;
	virtual void RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo) override;
	virtual int32 ServerReplicateActors(float DeltaSeconds) override;
	virtual void TearDown() override;
	//~ End UReplicationGraph

	/** True when the actor carries effective phase keys and therefore belongs in the phase node. */
	static bool ShouldRouteToPhaseNode(const AActor* Actor);

	/** Cell size of the main spatialization grid. */
	UPROPERTY(Config)
	float GridCellSize = 10000.0f;

	/** Cell size of the per-phase grids. */
	UPROPERTY(Config)
	float PhaseCellSize = 10000.0f;

	/** Spatialize phased actors per phase (grids) instead of gathering every actor of a phase (lists). */
	UPROPERTY(Config)
	bool bSpatializePhases = true;

	UPROPERTY()
	TObjectPtr<UReplicationGraphNode_GridSpatialization2D> GridNode;

	UPROPERTY()
	TObjectPtr<UReplicationGraphNode_ActorList> AlwaysRelevantNode;

	UPROPERTY()
	TObjectPtr<UActorPhasingReplicationGraphNode> PhaseNode;

	UPROPERTY()
	TArray<FActorPhasingConnectionNodePair> AlwaysRelevantForConnectionList;

	/** Owner-only actors that had no connection when they were added. */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> ActorsWithoutNetConnection;

	UReplicationGraphNode_AlwaysRelevant_ForConnection* GetAlwaysRelevantNodeForConnection(UNetConnection* Connection);

protected:
	virtual void InitializeForWorld(UWorld* World) override;
	virtual void RouteRenameNetworkActorToNodes(const FRenamedReplicatedActorInfo& ActorInfo) override;

	/** Re-routes an actor between the grid and the phase node when it gains or loses a phase. */
	void HandlePhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& OldKeys, const TArray<FActorPhaseKey>& NewKeys);

	void BindToSubsystem(UWorld* World);
	void UnbindFromSubsystem();

#if WITH_GAMEPLAY_DEBUGGER
	void OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner);
#endif

private:
	/** Actors currently routed to the phase node, so removal goes to the right place after a key change. */
	TSet<TObjectKey<AActor>> PhaseRoutedActors;

	TWeakObjectPtr<UActorPhaseSubsystem> BoundSubsystem;
	FDelegateHandle KeysChangedHandle;
};
