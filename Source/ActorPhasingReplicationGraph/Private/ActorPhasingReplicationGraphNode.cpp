// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhasingReplicationGraphNode.h"

#include "ActorPhaseMembershipComponent.h"
#include "ActorPhaseSubsystem.h"
#include "ActorPhasingLibrary.h"
#include "ActorPhasingModule.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UActorPhasingReplicationGraphNode::UActorPhasingReplicationGraphNode()
{
	// Child grids need PrepareForReplication every frame; the graph only calls it on root nodes with this flag.
	bRequiresPrepareForReplicationCall = true;
}

// ---------------------------------------------------------------------------------------------
// Bookkeeping
// ---------------------------------------------------------------------------------------------

UActorPhasingReplicationGraphNode::FPhaseChild& UActorPhasingReplicationGraphNode::FindOrAddChild(const FActorPhaseKey& Key, const bool bVisibleToObservers)
{
	FPhaseChild& Child = ChildrenFor(bVisibleToObservers).FindOrAdd(Key);
	const TCHAR* Kind = bVisibleToObservers ? TEXT("Observable") : TEXT("Members");
	if (!Child.List)
	{
		Child.List = CreateChildNode<UReplicationGraphNode_ActorList>(FName(*FString::Printf(TEXT("Phase_%s_%s_List"), *Key.ToString(), Kind)));
	}
	if (bSpatializePhases && !Child.Grid)
	{
		Child.Grid = CreateChildNode<UReplicationGraphNode_GridSpatialization2D>(FName(*FString::Printf(TEXT("Phase_%s_%s_Grid"), *Key.ToString(), Kind)));
		Child.Grid->CellSize = CellSize;
		Child.Grid->SpatialBias = SpatialBias;
	}
	return Child;
}

void UActorPhasingReplicationGraphNode::ReleaseChildIfEmpty(const FActorPhaseKey& Key, const bool bVisibleToObservers)
{
	TMap<FActorPhaseKey, FPhaseChild>& Children = ChildrenFor(bVisibleToObservers);
	FPhaseChild* Child = Children.Find(Key);
	if (!Child || Child->ActorCount > 0)
	{
		return;
	}
	if (Child->Grid)
	{
		RemoveChildNode(Child->Grid);
	}
	if (Child->List)
	{
		RemoveChildNode(Child->List);
	}
	Children.Remove(Key);
}

int32 UActorPhasingReplicationGraphNode::GetPhaseCount() const
{
	int32 Count = ChildrenByKey.Num();
	for (const TPair<FActorPhaseKey, FPhaseChild>& Pair : ObservableChildrenByKey)
	{
		if (!ChildrenByKey.Contains(Pair.Key))
		{
			++Count;
		}
	}
	return Count;
}

bool UActorPhasingReplicationGraphNode::IsTrackedAsObservable(const AActor* Actor) const
{
	const FTrackedActor* Tracked = TrackedActors.Find(Actor);
	return Tracked && Tracked->bVisibleToObservers;
}

bool UActorPhasingReplicationGraphNode::UsesListsFor(const AActor* Actor) const
{
	return !bSpatializePhases || (Actor && Actor->bAlwaysRelevant);
}

FGlobalActorReplicationInfo* UActorPhasingReplicationGraphNode::FindGlobalInfo(AActor* Actor) const
{
	if (GraphGlobals.IsValid() && GraphGlobals->GlobalActorReplicationInfoMap && Actor)
	{
		return &GraphGlobals->GlobalActorReplicationInfoMap->Get(Actor);
	}
	return nullptr;
}

void UActorPhasingReplicationGraphNode::FileActor(const FNewReplicatedActorInfo& ActorInfo, const TArray<FActorPhaseKey>& Keys, const bool bVisibleToObservers, FTrackedActor& Tracked)
{
	Tracked.StreamingLevelName = ActorInfo.StreamingLevelName;
	Tracked.Keys = Keys;
	Tracked.bInLists = UsesListsFor(ActorInfo.GetActor());
	Tracked.bVisibleToObservers = bVisibleToObservers && Keys.Num() > 0;

	if (Keys.Num() == 0)
	{
		if (!UnphasedList)
		{
			UnphasedList = CreateChildNode<UReplicationGraphNode_ActorList>(TEXT("Unphased"));
		}
		UnphasedList->NotifyAddNetworkActor(ActorInfo);
		return;
	}

	FGlobalActorReplicationInfo* GlobalInfo = Tracked.bInLists ? nullptr : FindGlobalInfo(ActorInfo.GetActor());
	if (!Tracked.bInLists && !GlobalInfo)
	{
		UE_LOG(LogActorPhasing, Warning, TEXT("%s: no global replication info available; filing %s in lists instead of grids."), *GetName(), *GetNameSafe(ActorInfo.GetActor()));
		Tracked.bInLists = true;
	}

	for (const FActorPhaseKey& Key : Keys)
	{
		FPhaseChild& Child = FindOrAddChild(Key, Tracked.bVisibleToObservers);
		if (Tracked.bInLists)
		{
			Child.List->NotifyAddNetworkActor(ActorInfo);
		}
		else
		{
			Child.Grid->AddActor_Dormancy(ActorInfo, *GlobalInfo);
		}
		++Child.ActorCount;
	}
}

void UActorPhasingReplicationGraphNode::UnfileActor(const FNewReplicatedActorInfo& ActorInfo, const FTrackedActor& Tracked)
{
	if (Tracked.Keys.Num() == 0)
	{
		if (UnphasedList)
		{
			UnphasedList->NotifyRemoveNetworkActor(ActorInfo, false);
		}
		return;
	}

	TMap<FActorPhaseKey, FPhaseChild>& Children = ChildrenFor(Tracked.bVisibleToObservers);
	for (const FActorPhaseKey& Key : Tracked.Keys)
	{
		FPhaseChild* Child = Children.Find(Key);
		if (!Child)
		{
			continue;
		}
		if (Tracked.bInLists)
		{
			if (Child->List)
			{
				Child->List->NotifyRemoveNetworkActor(ActorInfo, false);
			}
		}
		else if (Child->Grid)
		{
			Child->Grid->RemoveActor_Dormancy(ActorInfo);
		}
		Child->ActorCount = FMath::Max(0, Child->ActorCount - 1);
		ReleaseChildIfEmpty(Key, Tracked.bVisibleToObservers);
	}
}

TArray<FActorPhaseKey> UActorPhasingReplicationGraphNode::GetTrackedKeys(const AActor* Actor) const
{
	const FTrackedActor* Tracked = TrackedActors.Find(Actor);
	return Tracked ? Tracked->Keys : TArray<FActorPhaseKey>();
}

// ---------------------------------------------------------------------------------------------
// Node interface
// ---------------------------------------------------------------------------------------------

void UActorPhasingReplicationGraphNode::NotifyAddNetworkActor(const FNewReplicatedActorInfo& ActorInfo)
{
	AActor* Actor = ActorInfo.GetActor();
	if (!Actor)
	{
		return;
	}
	EnsureBound();

	if (FTrackedActor* Existing = TrackedActors.Find(Actor))
	{
		UnfileActor(FNewReplicatedActorInfo(Actor, Existing->StreamingLevelName), *Existing);
	}

	TArray<FActorPhaseKey> Keys;
	bool bVisibleToObservers = false;
	UActorPhasingLibrary::GetEffectivePhaseInfo(Actor, Keys, bVisibleToObservers);
	FileActor(ActorInfo, Keys, bVisibleToObservers, TrackedActors.FindOrAdd(Actor));
}

bool UActorPhasingReplicationGraphNode::NotifyRemoveNetworkActor(const FNewReplicatedActorInfo& ActorInfo, const bool bWarnIfNotFound)
{
	AActor* Actor = ActorInfo.GetActor();
	const FTrackedActor* Found = Actor ? TrackedActors.Find(Actor) : nullptr;
	if (!Found)
	{
		if (bWarnIfNotFound)
		{
			UE_LOG(LogActorPhasing, Warning, TEXT("%s: %s was not in the phase node."), *GetName(), *GetNameSafe(Actor));
		}
		return false;
	}
	const FTrackedActor Tracked = *Found;
	TrackedActors.Remove(Actor);
	UnfileActor(FNewReplicatedActorInfo(Actor, Tracked.StreamingLevelName), Tracked);
	return true;
}

bool UActorPhasingReplicationGraphNode::NotifyActorRenamed(const FRenamedReplicatedActorInfo& ActorInfo, const bool bWarnIfNotFound)
{
	AActor* Actor = ActorInfo.NewActorInfo.GetActor();
	FTrackedActor* Tracked = Actor ? TrackedActors.Find(Actor) : nullptr;
	if (!Tracked)
	{
		if (bWarnIfNotFound)
		{
			UE_LOG(LogActorPhasing, Warning, TEXT("%s: renamed actor %s was not in the phase node."), *GetName(), *GetNameSafe(Actor));
		}
		return false;
	}

	// Keys are unchanged; only the level name moves. Re-filing keeps the streaming-level lists correct.
	const FTrackedActor Copy = *Tracked;
	UnfileActor(FNewReplicatedActorInfo(Actor, Copy.StreamingLevelName), Copy);
	FileActor(ActorInfo.NewActorInfo, Copy.Keys, Copy.bVisibleToObservers, *Tracked);
	return true;
}

void UActorPhasingReplicationGraphNode::NotifyResetAllNetworkActors()
{
	Super::NotifyResetAllNetworkActors();
	TrackedActors.Empty();
	for (TPair<FActorPhaseKey, FPhaseChild>& Pair : ChildrenByKey)
	{
		Pair.Value.ActorCount = 0;
	}
	for (TPair<FActorPhaseKey, FPhaseChild>& Pair : ObservableChildrenByKey)
	{
		Pair.Value.ActorCount = 0;
	}
	// The world is changing: rebind lazily against the new one.
	Unbind();
}

void UActorPhasingReplicationGraphNode::PrepareForReplication()
{
	for (TPair<FActorPhaseKey, FPhaseChild>& Pair : ChildrenByKey)
	{
		if (Pair.Value.Grid)
		{
			Pair.Value.Grid->PrepareForReplication();
		}
	}
	for (TPair<FActorPhaseKey, FPhaseChild>& Pair : ObservableChildrenByKey)
	{
		if (Pair.Value.Grid)
		{
			Pair.Value.Grid->PrepareForReplication();
		}
	}
}

void UActorPhasingReplicationGraphNode::CollectViewerKeys(const FConnectionGatherActorListParameters& Params, TArray<FActorPhaseKey>& OutKeys) const
{
	const UActorPhaseSubsystem* Subsystem = BoundSubsystem.Get();
	for (const FNetViewer& Viewer : Params.Viewers)
	{
		const AActor* ViewerActor = Viewer.InViewer ? Viewer.InViewer.Get() : Viewer.ViewTarget.Get();
		const UActorPhaseMembershipComponent* Membership = (Subsystem && ViewerActor) ? Subsystem->FindMembership(ViewerActor) : nullptr;
		const TArray<FActorPhaseKey>& Keys = Membership ? Membership->GetMembershipRef() : UActorPhasingLibrary::GetBaseMembership();
		for (const FActorPhaseKey& Key : Keys)
		{
			OutKeys.AddUnique(Key);
		}
	}
}

void UActorPhasingReplicationGraphNode::CollectObservedKeys(const FConnectionGatherActorListParameters& Params, const TArray<FActorPhaseKey>& MemberKeys, TArray<FActorPhaseKey>& OutKeys) const
{
	const UActorPhaseSubsystem* Subsystem = BoundSubsystem.Get();
	if (!Subsystem)
	{
		return;
	}
	for (const FNetViewer& Viewer : Params.Viewers)
	{
		const AActor* ViewerActor = Viewer.InViewer ? Viewer.InViewer.Get() : Viewer.ViewTarget.Get();
		const UActorPhaseMembershipComponent* Membership = ViewerActor ? Subsystem->FindMembership(ViewerActor) : nullptr;
		if (!Membership)
		{
			continue;
		}
		for (const FActorPhaseKey& Key : Membership->GetObservedPhasesRef())
		{
			if (!MemberKeys.Contains(Key))
			{
				OutKeys.AddUnique(Key);
			}
		}
	}
}

namespace ActorPhasingReplicationGraphNodePrivate
{
	template <typename TChild>
	void GatherChild(const TChild* Child, const FConnectionGatherActorListParameters& Params)
	{
		if (!Child)
		{
			return;
		}
		if (Child->List)
		{
			Child->List->GatherActorListsForConnection(Params);
		}
		if (Child->Grid)
		{
			Child->Grid->GatherActorListsForConnection(Params);
		}
	}
}

void UActorPhasingReplicationGraphNode::GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params)
{
	using namespace ActorPhasingReplicationGraphNodePrivate;

	if (UnphasedList)
	{
		UnphasedList->GatherActorListsForConnection(Params);
	}
	if (ChildrenByKey.Num() == 0 && ObservableChildrenByKey.Num() == 0)
	{
		return;
	}
	EnsureBound();

	// Members see both halves of their phases.
	ViewerKeysScratch.Reset();
	CollectViewerKeys(Params, ViewerKeysScratch);
	for (const FActorPhaseKey& Key : ViewerKeysScratch)
	{
		GatherChild(ChildrenByKey.Find(Key), Params);
		GatherChild(ObservableChildrenByKey.Find(Key), Params);
	}

	// Observers see only the observable half of the phases they are not members of.
	if (ObservableChildrenByKey.Num() == 0)
	{
		return;
	}
	ObservedKeysScratch.Reset();
	CollectObservedKeys(Params, ViewerKeysScratch, ObservedKeysScratch);
	for (const FActorPhaseKey& Key : ObservedKeysScratch)
	{
		GatherChild(ObservableChildrenByKey.Find(Key), Params);
	}
}

void UActorPhasingReplicationGraphNode::TearDown()
{
	Unbind();
	TrackedActors.Empty();
	ChildrenByKey.Empty();
	ObservableChildrenByKey.Empty();
	UnphasedList = nullptr;
	Super::TearDown();
}

void UActorPhasingReplicationGraphNode::LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const
{
	DebugInfo.Log(FString::Printf(TEXT("%s (%d phases, %d actors, %s)"), *NodeName, GetPhaseCount(), TrackedActors.Num(), bSpatializePhases ? TEXT("grids") : TEXT("lists")));
	DebugInfo.PushIndent();
	for (const TPair<FActorPhaseKey, FPhaseChild>& Pair : ChildrenByKey)
	{
		DebugInfo.Log(FString::Printf(TEXT("%s: %d member-only actors"), *Pair.Key.ToString(), Pair.Value.ActorCount));
	}
	for (const TPair<FActorPhaseKey, FPhaseChild>& Pair : ObservableChildrenByKey)
	{
		DebugInfo.Log(FString::Printf(TEXT("%s: %d observable actors"), *Pair.Key.ToString(), Pair.Value.ActorCount));
	}
	if (UnphasedList)
	{
		UnphasedList->LogNode(DebugInfo, TEXT("Unphased"));
	}
	DebugInfo.PopIndent();
}

void UActorPhasingReplicationGraphNode::GetAllActorsInNode_Debugging(TArray<FActorRepListType>& OutArray) const
{
	for (const TPair<TObjectKey<AActor>, FTrackedActor>& Pair : TrackedActors)
	{
		if (AActor* Actor = Pair.Key.ResolveObjectPtr())
		{
			OutArray.Add(Actor);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Phase changes
// ---------------------------------------------------------------------------------------------

void UActorPhasingReplicationGraphNode::NotifyActorPhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& NewKeys)
{
	TArray<FActorPhaseKey> EffectiveKeys;
	bool bVisibleToObservers = false;
	UActorPhasingLibrary::GetEffectivePhaseInfo(Actor, EffectiveKeys, bVisibleToObservers);
	NotifyActorPhaseKeysChanged(Actor, NewKeys, bVisibleToObservers);
}

void UActorPhasingReplicationGraphNode::NotifyActorPhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& NewKeys, const bool bVisibleToObservers)
{
	FTrackedActor* Tracked = Actor ? TrackedActors.Find(Actor) : nullptr;
	const bool bObservable = bVisibleToObservers && NewKeys.Num() > 0;
	if (!Tracked || (Tracked->Keys == NewKeys && Tracked->bVisibleToObservers == bObservable))
	{
		return;
	}
	const FNewReplicatedActorInfo Info(Actor, Tracked->StreamingLevelName);
	const FTrackedActor Copy = *Tracked;
	UnfileActor(Info, Copy);
	FileActor(Info, NewKeys, bObservable, *Tracked);
}

void UActorPhasingReplicationGraphNode::HandlePhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& OldKeys, const TArray<FActorPhaseKey>& NewKeys)
{
	NotifyActorPhaseKeysChanged(Actor, NewKeys);
}

void UActorPhasingReplicationGraphNode::EnsureBound()
{
	if (BoundSubsystem.IsValid())
	{
		return;
	}
	UWorld* World = GetWorld();
	UActorPhaseSubsystem* Subsystem = World ? World->GetSubsystem<UActorPhaseSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}
	KeysChangedHandle = Subsystem->OnActorPhaseKeysChangedNative.AddUObject(this, &UActorPhasingReplicationGraphNode::HandlePhaseKeysChanged);
	BoundSubsystem = Subsystem;
}

void UActorPhasingReplicationGraphNode::Unbind()
{
	if (UActorPhaseSubsystem* Subsystem = BoundSubsystem.Get())
	{
		Subsystem->OnActorPhaseKeysChangedNative.Remove(KeysChangedHandle);
	}
	KeysChangedHandle.Reset();
	BoundSubsystem.Reset();
}
