// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ActorPhaseComponent.h"
#include "ActorPhaseKey.h"
#include "ActorPhaseMembershipComponent.h"
#include "ActorPhasingReplicationGraph.h"
#include "ActorPhasingReplicationGraphNode.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "ReplicationGraph.h"
#include "ReplicationGraphTypes.h"

/**
 * Node-level tests. They run the node with actor-list children (no spatialization) against a bare
 * game world, which exercises routing, gathering and re-filing without a net driver. The grid
 * children are the engine's own node and are covered by its tests.
 */
namespace ActorPhasingReplicationGraphTests
{
	UWorld* CreateTestWorld(const TCHAR* Name)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
		if (World)
		{
			World->InitializeActorsForPlay(FURL());
			World->SetBegunPlay(true);
		}
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			CollectGarbage(RF_NoFlags);
		}
	}

	struct FTestPlayer
	{
		APlayerController* Controller = nullptr;
		APlayerState* PlayerState = nullptr;
		UActorPhaseMembershipComponent* Membership = nullptr;
	};

	FTestPlayer SpawnPlayer(UWorld* World)
	{
		FTestPlayer Player;
		Player.PlayerState = World->SpawnActor<APlayerState>();
		Player.Controller = World->SpawnActor<APlayerController>();
		if (!Player.PlayerState || !Player.Controller)
		{
			return Player;
		}
		Player.Membership = NewObject<UActorPhaseMembershipComponent>(Player.PlayerState, UActorPhaseMembershipComponent::StaticClass(), TEXT("ActorPhaseMembership"));
		Player.Membership->RegisterComponent();
		Player.Controller->PlayerState = Player.PlayerState;
		Player.PlayerState->SetOwner(Player.Controller);
		return Player;
	}

	/** A replicated actor with a phase component (empty keys = unphased). */
	AActor* SpawnReplicatedActor(UWorld* World, const TArray<FActorPhaseKey>& Keys)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetReplicates(true);

		UActorPhaseComponent* Phase = NewObject<UActorPhaseComponent>(Actor, UActorPhaseComponent::StaticClass(), TEXT("ActorPhase"));
		Phase->RegisterComponent();
		Phase->SetPhaseKeys(Keys);
		return Actor;
	}

	UActorPhasingReplicationGraphNode* CreateListNode(UWorld* World)
	{
		UActorPhasingReplicationGraphNode* Node = NewObject<UActorPhasingReplicationGraphNode>(GetTransientPackage());
		Node->bSpatializePhases = false;
		Node->Initialize(MakeShared<FReplicationGraphGlobalData>(nullptr, World, nullptr));
		return Node;
	}

	/** Gathers for one viewer and returns the actors the node produced. */
	TArray<AActor*> Gather(UActorPhasingReplicationGraphNode* Node, UNetReplicationGraphConnection* ConnectionManager, APlayerController* Viewer)
	{
		FNetViewerArray Viewers;
		FNetViewer& NetViewer = Viewers.AddDefaulted_GetRef();
		NetViewer.InViewer = Viewer;
		NetViewer.ViewTarget = Viewer;

		TSet<FName> VisibleLevels;
		FGatheredReplicationActorLists Lists;
		FConnectionGatherActorListParameters Params(Viewers, *ConnectionManager, VisibleLevels, 1, Lists, false);
		Node->GatherActorListsForConnection(Params);

		TArray<AActor*> Result;
		for (const FActorRepListType& Actor : Lists.ViewActors(EActorRepListTypeFlags::Default))
		{
			Result.Add(Actor);
		}
		return Result;
	}

	FActorPhaseKey Key(const TCHAR* Domain, const int64 Id)
	{
		return FActorPhaseKey::Make(FName(Domain), Id);
	}
}

using namespace ActorPhasingReplicationGraphTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingRepGraphGatherTest,
	"ActorPhasing.ReplicationGraph.NodeGathersMatchingPhaseOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingRepGraphGatherTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingRepGraphGather"));
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);

	UActorPhasingReplicationGraphNode* Node = CreateListNode(World);
	UNetReplicationGraphConnection* ConnectionManager = NewObject<UNetReplicationGraphConnection>(GetTransientPackage());

	AActor* InX = SpawnReplicatedActor(World, { X });
	AActor* InY = SpawnReplicatedActor(World, { Y });
	AActor* InBoth = SpawnReplicatedActor(World, { X, Y });
	AActor* Unphased = SpawnReplicatedActor(World, {});
	Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(InX));
	Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(InY));
	Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(InBoth));
	Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(Unphased));

	TestEqual(TEXT("Two phases have children"), Node->GetPhaseCount(), 2);
	TestEqual(TEXT("Four actors tracked"), Node->GetTrackedActorCount(), 4);

	FTestPlayer BaseViewer = SpawnPlayer(World);
	FTestPlayer XViewer = SpawnPlayer(World);
	FTestPlayer YViewer = SpawnPlayer(World);
	XViewer.Membership->SetSinglePhase(X);
	YViewer.Membership->SetSinglePhase(Y);

	{
		const TArray<AActor*> Gathered = Gather(Node, ConnectionManager, BaseViewer.Controller);
		TestEqual(TEXT("Base viewer gathers only the unphased actor"), Gathered.Num(), 1);
		TestTrue(TEXT("Base viewer gathers the unphased actor"), Gathered.Contains(Unphased));
	}
	{
		const TArray<AActor*> Gathered = Gather(Node, ConnectionManager, XViewer.Controller);
		TestTrue(TEXT("X viewer gathers the X actor"), Gathered.Contains(InX));
		TestTrue(TEXT("X viewer gathers the X+Y actor"), Gathered.Contains(InBoth));
		TestTrue(TEXT("X viewer gathers the unphased actor"), Gathered.Contains(Unphased));
		TestFalse(TEXT("X viewer does not gather the Y actor"), Gathered.Contains(InY));
	}
	{
		const TArray<AActor*> Gathered = Gather(Node, ConnectionManager, YViewer.Controller);
		TestTrue(TEXT("Y viewer gathers the Y actor"), Gathered.Contains(InY));
		TestFalse(TEXT("Y viewer does not gather the X actor"), Gathered.Contains(InX));
	}

	Node->TearDown();
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingRepGraphRefileTest,
	"ActorPhasing.ReplicationGraph.KeyChangeRefilesActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingRepGraphRefileTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingRepGraphRefile"));
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);

	UActorPhasingReplicationGraphNode* Node = CreateListNode(World);
	UNetReplicationGraphConnection* ConnectionManager = NewObject<UNetReplicationGraphConnection>(GetTransientPackage());

	AActor* Actor = SpawnReplicatedActor(World, { X });
	Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(Actor));

	FTestPlayer XViewer = SpawnPlayer(World);
	FTestPlayer YViewer = SpawnPlayer(World);
	XViewer.Membership->SetSinglePhase(X);
	YViewer.Membership->SetSinglePhase(Y);

	TestTrue(TEXT("Filed under X before the change"), Gather(Node, ConnectionManager, XViewer.Controller).Contains(Actor));
	TestFalse(TEXT("Not filed under Y before the change"), Gather(Node, ConnectionManager, YViewer.Controller).Contains(Actor));

	// The component change reaches the node through the subsystem event.
	UActorPhaseComponent* Phase = Actor->FindComponentByClass<UActorPhaseComponent>();
	Phase->SetSinglePhase(Y);

	TestFalse(TEXT("Not filed under X after the change"), Gather(Node, ConnectionManager, XViewer.Controller).Contains(Actor));
	TestTrue(TEXT("Filed under Y after the change"), Gather(Node, ConnectionManager, YViewer.Controller).Contains(Actor));
	TestEqual(TEXT("Empty phase child released"), Node->GetPhaseCount(), 1);
	TestEqual(TEXT("Tracked keys follow the component"), Node->GetTrackedKeys(Actor), TArray<FActorPhaseKey>{ Y });

	// Losing every key parks the actor in the unphased list, visible to everyone, until the graph re-routes it.
	Phase->ClearPhaseKeys();
	TestTrue(TEXT("Unphased actor gathered for X viewer"), Gather(Node, ConnectionManager, XViewer.Controller).Contains(Actor));
	TestTrue(TEXT("Unphased actor gathered for Y viewer"), Gather(Node, ConnectionManager, YViewer.Controller).Contains(Actor));
	TestEqual(TEXT("No phase children remain"), Node->GetPhaseCount(), 0);

	TestTrue(TEXT("Removal succeeds"), Node->NotifyRemoveNetworkActor(FNewReplicatedActorInfo(Actor)));
	TestEqual(TEXT("Nothing tracked after removal"), Node->GetTrackedActorCount(), 0);
	TestTrue(TEXT("Nothing gathered after removal"), Gather(Node, ConnectionManager, XViewer.Controller).Num() == 0);

	Node->TearDown();
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingRepGraphMembershipTest,
	"ActorPhasing.ReplicationGraph.MembershipChangeSwitchesGatheredPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingRepGraphMembershipTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingRepGraphMembership"));
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);

	UActorPhasingReplicationGraphNode* Node = CreateListNode(World);
	UNetReplicationGraphConnection* ConnectionManager = NewObject<UNetReplicationGraphConnection>(GetTransientPackage());

	AActor* InX = SpawnReplicatedActor(World, { X });
	AActor* InY = SpawnReplicatedActor(World, { Y });
	Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(InX));
	Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(InY));

	FTestPlayer Viewer = SpawnPlayer(World);
	TestEqual(TEXT("Base viewer gathers nothing phased"), Gather(Node, ConnectionManager, Viewer.Controller).Num(), 0);

	Viewer.Membership->SetSinglePhase(X);
	TestEqual(TEXT("X membership gathers exactly the X actor"), Gather(Node, ConnectionManager, Viewer.Controller), TArray<AActor*>{ InX });

	Viewer.Membership->SetMembership({ X, Y });
	{
		const TArray<AActor*> Gathered = Gather(Node, ConnectionManager, Viewer.Controller);
		TestEqual(TEXT("X+Y membership gathers both"), Gathered.Num(), 2);
	}

	Viewer.Membership->ResetToBase();
	TestEqual(TEXT("Back in base gathers nothing phased"), Gather(Node, ConnectionManager, Viewer.Controller).Num(), 0);

	Node->TearDown();
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingRepGraphRoutingTest,
	"ActorPhasing.ReplicationGraph.ShouldRouteToPhaseNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingRepGraphRoutingTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingRepGraphRouting"));
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	AActor* Phased = SpawnReplicatedActor(World, { Key(TEXT("Test"), 7) });
	AActor* Unphased = SpawnReplicatedActor(World, {});
	AActor* Plain = World->SpawnActor<AActor>();

	TestTrue(TEXT("Phased actor routes to the phase node"), UActorPhasingReplicationGraph::ShouldRouteToPhaseNode(Phased));
	TestFalse(TEXT("Component without keys routes normally"), UActorPhasingReplicationGraph::ShouldRouteToPhaseNode(Unphased));
	TestFalse(TEXT("Actor without component routes normally"), UActorPhasingReplicationGraph::ShouldRouteToPhaseNode(Plain));
	TestFalse(TEXT("Null routes normally"), UActorPhasingReplicationGraph::ShouldRouteToPhaseNode(nullptr));

	// Owner inheritance: an unphased actor owned by a phased one follows it into the node.
	Plain->SetOwner(Phased);
	TestTrue(TEXT("Owned actor inherits the phase route"), UActorPhasingReplicationGraph::ShouldRouteToPhaseNode(Plain));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingRepGraphObservationTest,
	"ActorPhasing.ReplicationGraph.ObserversGatherObservableActorsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingRepGraphObservationTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingRepGraphObserve"));
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	const FActorPhaseKey X = Key(TEXT("Test"), 1);

	UActorPhasingReplicationGraphNode* Node = CreateListNode(World);
	UNetReplicationGraphConnection* ConnectionManager = NewObject<UNetReplicationGraphConnection>(GetTransientPackage());

	AActor* Prop = SpawnReplicatedActor(World, { X });
	AActor* Hidden = SpawnReplicatedActor(World, { X });
	Hidden->FindComponentByClass<UActorPhaseComponent>()->SetVisibleToObservers(false);
	Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(Prop));
	Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(Hidden));

	TestTrue(TEXT("Prop is filed as observable"), Node->IsTrackedAsObservable(Prop));
	TestFalse(TEXT("Hidden actor is filed as member-only"), Node->IsTrackedAsObservable(Hidden));
	TestEqual(TEXT("Both halves count as one phase"), Node->GetPhaseCount(), 1);

	FTestPlayer Member = SpawnPlayer(World);
	FTestPlayer Observer = SpawnPlayer(World);
	Member.Membership->SetSinglePhase(X);
	Observer.Membership->AddObservedPhase(X);

	{
		const TArray<AActor*> Gathered = Gather(Node, ConnectionManager, Member.Controller);
		TestTrue(TEXT("Member gathers the prop"), Gathered.Contains(Prop));
		TestTrue(TEXT("Member gathers the member-only actor"), Gathered.Contains(Hidden));
	}
	{
		const TArray<AActor*> Gathered = Gather(Node, ConnectionManager, Observer.Controller);
		TestTrue(TEXT("Observer gathers the prop"), Gathered.Contains(Prop));
		TestFalse(TEXT("Observer does not gather the member-only actor"), Gathered.Contains(Hidden));
	}

	// Observability changes re-file through the subsystem event.
	Prop->FindComponentByClass<UActorPhaseComponent>()->SetVisibleToObservers(false);
	TestFalse(TEXT("Re-filed as member-only"), Node->IsTrackedAsObservable(Prop));
	TestFalse(TEXT("Observer loses the prop"), Gather(Node, ConnectionManager, Observer.Controller).Contains(Prop));

	Node->TearDown();
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
