// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhasingReplicationGraph.h"

#include "ActorPhaseSubsystem.h"
#include "ActorPhasingLibrary.h"
#include "ActorPhasingModule.h"
#include "ActorPhasingReplicationGraphNode.h"
#include "Engine/ChildConnection.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameFramework/PlayerController.h"
#include "GameplayDebuggerCategoryReplicator.h"
#endif

namespace ActorPhasingReplicationGraphPrivate
{
	static UNetConnection* ResolveParentConnection(UNetConnection* Connection)
	{
		if (Connection && Connection->GetUChildConnection() != nullptr)
		{
			return static_cast<UChildConnection*>(Connection)->Parent;
		}
		return Connection;
	}
}

UActorPhasingReplicationGraph::UActorPhasingReplicationGraph()
{
}

bool UActorPhasingReplicationGraph::ShouldRouteToPhaseNode(const AActor* Actor)
{
	TArray<FActorPhaseKey> Keys;
	return Actor && UActorPhasingLibrary::GetEffectivePhaseKeys(Actor, Keys) && Keys.Num() > 0;
}

// ---------------------------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------------------------

void UActorPhasingReplicationGraph::InitGlobalActorClassSettings()
{
	Super::InitGlobalActorClassSettings();

	// Same policy as the engine's basic graph: class settings come from the CDOs.
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		const AActor* ActorCDO = Cast<AActor>(Class->GetDefaultObject());
		if (!ActorCDO || !ActorCDO->GetIsReplicated())
		{
			continue;
		}
		if (Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
		{
			continue;
		}

		FClassReplicationInfo ClassInfo;
		ClassInfo.ReplicationPeriodFrame = GetReplicationPeriodFrameForFrequency(ActorCDO->GetNetUpdateFrequency());
		if (ActorCDO->bAlwaysRelevant || ActorCDO->bOnlyRelevantToOwner)
		{
			ClassInfo.SetCullDistanceSquared(0.0f);
		}
		else
		{
			ClassInfo.SetCullDistanceSquared(ActorCDO->GetNetCullDistanceSquared());
		}
		GlobalActorReplicationInfoMap.SetClassInfo(Class, ClassInfo);
	}

#if WITH_GAMEPLAY_DEBUGGER
	AGameplayDebuggerCategoryReplicator::NotifyDebuggerOwnerChange.AddUObject(this, &ThisClass::OnGameplayDebuggerOwnerChange);
#endif
}

void UActorPhasingReplicationGraph::InitGlobalGraphNodes()
{
	GridNode = CreateNewNode<UReplicationGraphNode_GridSpatialization2D>();
	GridNode->CellSize = GridCellSize;
	GridNode->SpatialBias = FVector2D(-UE_OLD_WORLD_MAX, -UE_OLD_WORLD_MAX);
	AddGlobalGraphNode(GridNode);

	AlwaysRelevantNode = CreateNewNode<UReplicationGraphNode_ActorList>();
	AddGlobalGraphNode(AlwaysRelevantNode);

	PhaseNode = CreateNewNode<UActorPhasingReplicationGraphNode>();
	PhaseNode->bSpatializePhases = bSpatializePhases;
	PhaseNode->CellSize = PhaseCellSize;
	PhaseNode->SpatialBias = FVector2D(-UE_OLD_WORLD_MAX, -UE_OLD_WORLD_MAX);
	AddGlobalGraphNode(PhaseNode);
}

void UActorPhasingReplicationGraph::InitConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection)
{
	Super::InitConnectionGraphNodes(RepGraphConnection);

	UReplicationGraphNode_AlwaysRelevant_ForConnection* Node = CreateNewNode<UReplicationGraphNode_AlwaysRelevant_ForConnection>();
	AddConnectionGraphNode(Node, RepGraphConnection);

	FActorPhasingConnectionNodePair& Pair = AlwaysRelevantForConnectionList.AddDefaulted_GetRef();
	Pair.NetConnection = RepGraphConnection->NetConnection;
	Pair.Node = Node;
}

void UActorPhasingReplicationGraph::InitializeForWorld(UWorld* World)
{
	Super::InitializeForWorld(World);
	PhaseRoutedActors.Empty();
	BindToSubsystem(World);
}

void UActorPhasingReplicationGraph::TearDown()
{
	UnbindFromSubsystem();
	PhaseRoutedActors.Empty();
	Super::TearDown();
}

void UActorPhasingReplicationGraph::BindToSubsystem(UWorld* World)
{
	UnbindFromSubsystem();
	UActorPhaseSubsystem* Subsystem = World ? World->GetSubsystem<UActorPhaseSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}
	KeysChangedHandle = Subsystem->OnActorPhaseKeysChangedNative.AddUObject(this, &UActorPhasingReplicationGraph::HandlePhaseKeysChanged);
	BoundSubsystem = Subsystem;
}

void UActorPhasingReplicationGraph::UnbindFromSubsystem()
{
	if (UActorPhaseSubsystem* Subsystem = BoundSubsystem.Get())
	{
		Subsystem->OnActorPhaseKeysChangedNative.Remove(KeysChangedHandle);
	}
	KeysChangedHandle.Reset();
	BoundSubsystem.Reset();
}

// ---------------------------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------------------------

void UActorPhasingReplicationGraph::RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo)
{
	AActor* Actor = ActorInfo.GetActor();
#if WITH_GAMEPLAY_DEBUGGER
	if (Actor->IsA(AGameplayDebuggerCategoryReplicator::StaticClass()))
	{
		return;
	}
#endif

	ensureMsgf((Actor->bAlwaysRelevant && Actor->bOnlyRelevantToOwner) == false, TEXT("Replicated actor %s is both bAlwaysRelevant and bOnlyRelevantToOwner. Only one can be supported."), *Actor->GetName());

	if (Actor->bOnlyRelevantToOwner)
	{
		ActorsWithoutNetConnection.Add(Actor);
	}
	else if (ShouldRouteToPhaseNode(Actor))
	{
		// Phased actors, always-relevant or not, are visible only inside their phases.
		PhaseNode->NotifyAddNetworkActor(ActorInfo);
		PhaseRoutedActors.Add(Actor);
	}
	else if (Actor->bAlwaysRelevant)
	{
		AlwaysRelevantNode->NotifyAddNetworkActor(ActorInfo);
	}
	else
	{
		GridNode->AddActor_Dormancy(ActorInfo, GlobalInfo);
	}
}

void UActorPhasingReplicationGraph::RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo)
{
	AActor* Actor = ActorInfo.GetActor();
#if WITH_GAMEPLAY_DEBUGGER
	if (Actor->IsA(AGameplayDebuggerCategoryReplicator::StaticClass()))
	{
		return;
	}
#endif

	if (PhaseRoutedActors.Remove(Actor) > 0)
	{
		PhaseNode->NotifyRemoveNetworkActor(ActorInfo);
		if (Actor->bAlwaysRelevant)
		{
			SetActorDestructionInfoToIgnoreDistanceCulling(Actor);
		}
	}
	else if (Actor->bAlwaysRelevant)
	{
		AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
		SetActorDestructionInfoToIgnoreDistanceCulling(Actor);
	}
	else if (Actor->bOnlyRelevantToOwner)
	{
		ActorsWithoutNetConnection.Remove(Actor);
		if (UReplicationGraphNode* Node = Actor->GetNetConnection() ? GetAlwaysRelevantNodeForConnection(Actor->GetNetConnection()) : nullptr)
		{
			Node->NotifyRemoveNetworkActor(ActorInfo);
		}
	}
	else
	{
		GridNode->RemoveActor_Dormancy(ActorInfo);
	}
}

void UActorPhasingReplicationGraph::RouteRenameNetworkActorToNodes(const FRenamedReplicatedActorInfo& ActorInfo)
{
	AActor* Actor = ActorInfo.NewActorInfo.GetActor();
#if WITH_GAMEPLAY_DEBUGGER
	if (Actor->IsA(AGameplayDebuggerCategoryReplicator::StaticClass()))
	{
		return;
	}
#endif

	if (PhaseRoutedActors.Contains(Actor))
	{
		PhaseNode->NotifyActorRenamed(ActorInfo);
	}
	else if (Actor->bAlwaysRelevant)
	{
		AlwaysRelevantNode->NotifyActorRenamed(ActorInfo);
	}
	else if (Actor->bOnlyRelevantToOwner)
	{
		if (UReplicationGraphNode* Node = Actor->GetNetConnection() ? GetAlwaysRelevantNodeForConnection(Actor->GetNetConnection()) : nullptr)
		{
			Node->NotifyActorRenamed(ActorInfo);
		}
	}
	else
	{
		GridNode->RenameActor_Dormancy(ActorInfo);
	}
}

void UActorPhasingReplicationGraph::HandlePhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& OldKeys, const TArray<FActorPhaseKey>& NewKeys)
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->bOnlyRelevantToOwner)
	{
		return;
	}
	FGlobalActorReplicationInfo* GlobalInfo = GlobalActorReplicationInfoMap.Find(Actor);
	if (!GlobalInfo)
	{
		return; // not in the graph (yet); RouteAddNetworkActorToNodes will decide when it arrives
	}

	const bool bWasPhaseRouted = PhaseRoutedActors.Contains(Actor);
	const bool bShouldBePhaseRouted = ShouldRouteToPhaseNode(Actor);
	if (bWasPhaseRouted == bShouldBePhaseRouted)
	{
		return; // phased -> phased: the node re-files the actor itself
	}

	const FNewReplicatedActorInfo Info(Actor);
	RouteRemoveNetworkActorToNodes(Info);
	RouteAddNetworkActorToNodes(Info, *GlobalInfo);
}

UReplicationGraphNode_AlwaysRelevant_ForConnection* UActorPhasingReplicationGraph::GetAlwaysRelevantNodeForConnection(UNetConnection* Connection)
{
	Connection = ActorPhasingReplicationGraphPrivate::ResolveParentConnection(Connection);
	if (!Connection)
	{
		UE_LOG(LogActorPhasing, Warning, TEXT("bOnlyRelevantToOwner actor has no owning connection; it will not replicate."));
		return nullptr;
	}
	for (const FActorPhasingConnectionNodePair& Pair : AlwaysRelevantForConnectionList)
	{
		if (Pair.NetConnection == Connection)
		{
			return Pair.Node;
		}
	}
	UE_LOG(LogActorPhasing, Warning, TEXT("No always-relevant node for connection %s."), *GetNameSafe(Connection));
	return nullptr;
}

int32 UActorPhasingReplicationGraph::ServerReplicateActors(const float DeltaSeconds)
{
	for (int32 Index = ActorsWithoutNetConnection.Num() - 1; Index >= 0; --Index)
	{
		bool bRemove = false;
		if (AActor* Actor = ActorsWithoutNetConnection[Index])
		{
			if (UNetConnection* Connection = Actor->GetNetConnection())
			{
				bRemove = true;
				if (UReplicationGraphNode_AlwaysRelevant_ForConnection* Node = GetAlwaysRelevantNodeForConnection(Connection))
				{
					Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(Actor));
				}
			}
		}
		else
		{
			bRemove = true;
		}
		if (bRemove)
		{
			ActorsWithoutNetConnection.RemoveAtSwap(Index, EAllowShrinking::No);
		}
	}

	return Super::ServerReplicateActors(DeltaSeconds);
}

#if WITH_GAMEPLAY_DEBUGGER
void UActorPhasingReplicationGraph::OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner)
{
	if (!Debugger || Debugger->GetWorld() != GetWorld())
	{
		return;
	}
	const FNewReplicatedActorInfo ActorInfo(Debugger);
	if (OldOwner)
	{
		if (UReplicationGraphNode* Node = OldOwner->GetNetConnection() ? GetAlwaysRelevantNodeForConnection(OldOwner->GetNetConnection()) : nullptr)
		{
			Node->NotifyRemoveNetworkActor(ActorInfo);
		}
	}
	if (APlayerController* NewOwner = Debugger->GetReplicationOwner())
	{
		if (UReplicationGraphNode* Node = NewOwner->GetNetConnection() ? GetAlwaysRelevantNodeForConnection(NewOwner->GetNetConnection()) : nullptr)
		{
			Node->NotifyAddNetworkActor(ActorInfo);
		}
	}
}
#endif
