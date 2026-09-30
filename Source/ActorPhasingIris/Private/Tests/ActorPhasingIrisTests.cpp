// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ActorPhaseKey.h"
#include "ActorPhasingIrisGroupTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

/**
 * Group-table tests against a recording backend. The Iris calls themselves are thin wrappers
 * (see FIrisGroupBackend); the bookkeeping is where the rule lives.
 */
namespace ActorPhasingIrisTests
{
	class FRecordingBackend final : public IActorPhasingIrisGroupBackend
	{
	public:
		struct FGroupState
		{
			FString Name;
			TSet<TObjectKey<AActor>> Members;
			TMap<uint32, bool> Status;
		};

		/** Actors the "replication system" knows about; others have no handle yet. */
		TSet<TObjectKey<AActor>> ReplicatingActors;
		TMap<uint32, FGroupState> Groups;
		int32 CreatedGroups = 0;
		int32 DestroyedGroups = 0;
		bool bFailCreate = false;

		virtual uint32 CreateGroup(const FString& DebugName) override
		{
			if (bFailCreate)
			{
				return 0;
			}
			const uint32 Id = ++CreatedGroups;
			Groups.Add(Id).Name = DebugName;
			return Id;
		}

		virtual void DestroyGroup(const uint32 GroupId) override
		{
			++DestroyedGroups;
			Groups.Remove(GroupId);
		}

		virtual bool AddActorToGroup(const uint32 GroupId, AActor* Actor) override
		{
			if (!ReplicatingActors.Contains(Actor))
			{
				return false;
			}
			Groups.FindChecked(GroupId).Members.Add(Actor);
			return true;
		}

		virtual void RemoveActorFromGroup(const uint32 GroupId, AActor* Actor) override
		{
			if (FGroupState* Group = Groups.Find(GroupId))
			{
				Group->Members.Remove(Actor);
			}
		}

		virtual void SetGroupStatus(const uint32 GroupId, const uint32 ConnectionId, const bool bAllow) override
		{
			Groups.FindChecked(GroupId).Status.Add(ConnectionId, bAllow);
		}

		bool IsAllowed(const uint32 GroupId, const uint32 ConnectionId) const
		{
			const FGroupState* Group = Groups.Find(GroupId);
			const bool* Status = Group ? Group->Status.Find(ConnectionId) : nullptr;
			return Status ? *Status : false;
		}
	};

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

	FActorPhaseKey Key(const TCHAR* Domain, const int64 Id)
	{
		return FActorPhaseKey::Make(FName(Domain), Id);
	}
}

using namespace ActorPhasingIrisTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingIrisKeySetTest,
	"ActorPhasing.Iris.KeySetNormalizes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingIrisKeySetTest::RunTest(const FString& Parameters)
{
	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);

	const FActorPhasingIrisKeySet A = FActorPhasingIrisKeySet::From({ Y, X, X, FActorPhaseKey() });
	const FActorPhasingIrisKeySet B = FActorPhasingIrisKeySet::From({ X, Y });
	TestEqual(TEXT("Order and duplicates do not matter"), A, B);
	TestEqual(TEXT("Hashes agree"), GetTypeHash(A), GetTypeHash(B));
	TestEqual(TEXT("Two valid keys"), A.Keys.Num(), 2);
	TestTrue(TEXT("Invalid keys dropped -> empty set is empty"), FActorPhasingIrisKeySet::From({ FActorPhaseKey() }).IsEmpty());
	TestTrue(TEXT("Intersects on a shared key"), A.Intersects({ Y }));
	TestFalse(TEXT("Disjoint membership does not intersect"), A.Intersects({ Key(TEXT("Test"), 3) }));
	TestEqual(TEXT("Describes as Domain:Id joined"), A.ToString(), FString(TEXT("Test:1+Test:2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingIrisGroupingTest,
	"ActorPhasing.Iris.ActorsShareGroupPerKeySet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingIrisGroupingTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingIrisGrouping"));
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);

	FRecordingBackend Backend;
	FActorPhasingIrisGroupTable Table(Backend);

	AActor* A = World->SpawnActor<AActor>();
	AActor* B = World->SpawnActor<AActor>();
	AActor* C = World->SpawnActor<AActor>();
	Backend.ReplicatingActors.Add(A);
	Backend.ReplicatingActors.Add(B);
	Backend.ReplicatingActors.Add(C);

	Table.SetActorKeys(A, { X });
	Table.SetActorKeys(B, { X });
	Table.SetActorKeys(C, { X, Y });

	TestEqual(TEXT("Two distinct key sets -> two groups"), Table.GetGroupCount(), 2);
	TestEqual(TEXT("A and B share a group"), Table.FindGroupIdForActor(A), Table.FindGroupIdForActor(B));
	TestTrue(TEXT("C has its own group"), Table.FindGroupIdForActor(A) != Table.FindGroupIdForActor(C));
	TestTrue(TEXT("A grouped"), Table.IsActorGrouped(A));
	TestEqual(TEXT("Nothing pending"), Table.GetPendingCount(), 0);

	// Same keys again is a no-op.
	const int32 CreatedBefore = Backend.CreatedGroups;
	Table.SetActorKeys(A, { X });
	TestEqual(TEXT("Idempotent for unchanged keys"), Backend.CreatedGroups, CreatedBefore);

	// Removing the last member of a set destroys its group.
	Table.RemoveActor(C);
	TestEqual(TEXT("Empty group destroyed"), Table.GetGroupCount(), 1);
	TestEqual(TEXT("Backend saw the destroy"), Backend.DestroyedGroups, 1);

	Table.Reset();
	TestEqual(TEXT("Reset destroys every group"), Backend.Groups.Num(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingIrisStatusTest,
	"ActorPhasing.Iris.MembershipDrivesGroupStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingIrisStatusTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingIrisStatus"));
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);
	const uint32 Conn1 = 1;
	const uint32 Conn2 = 2;

	FRecordingBackend Backend;
	FActorPhasingIrisGroupTable Table(Backend);

	AActor* InX = World->SpawnActor<AActor>();
	AActor* InXY = World->SpawnActor<AActor>();
	Backend.ReplicatingActors.Add(InX);
	Backend.ReplicatingActors.Add(InXY);

	// Connections known before the groups exist get statuses when the group is created.
	Table.SetConnectionMembership(Conn1, { X });
	Table.SetConnectionMembership(Conn2, { FActorPhaseKey::Base() });
	Table.SetActorKeys(InX, { X });
	Table.SetActorKeys(InXY, { X, Y });

	const uint32 GroupX = Table.FindGroupIdForActor(InX);
	const uint32 GroupXY = Table.FindGroupIdForActor(InXY);
	TestTrue(TEXT("Conn1 (X) allowed for group X"), Backend.IsAllowed(GroupX, Conn1));
	TestTrue(TEXT("Conn1 (X) allowed for group X+Y"), Backend.IsAllowed(GroupXY, Conn1));
	TestFalse(TEXT("Conn2 (base) disallowed for group X"), Backend.IsAllowed(GroupX, Conn2));
	TestFalse(TEXT("Conn2 (base) disallowed for group X+Y"), Backend.IsAllowed(GroupXY, Conn2));

	// A membership change updates every group for that connection.
	Table.SetConnectionMembership(Conn2, { Y });
	TestFalse(TEXT("Conn2 (Y) still disallowed for group X"), Backend.IsAllowed(GroupX, Conn2));
	TestTrue(TEXT("Conn2 (Y) allowed for group X+Y"), Backend.IsAllowed(GroupXY, Conn2));
	TestTrue(TEXT("Query agrees"), Table.IsActorAllowedForConnection(InXY, Conn2));
	TestFalse(TEXT("Query agrees for X"), Table.IsActorAllowedForConnection(InX, Conn2));

	// Back to base closes everything.
	Table.SetConnectionMembership(Conn1, { FActorPhaseKey::Base() });
	TestFalse(TEXT("Conn1 back in base disallowed for X"), Backend.IsAllowed(GroupX, Conn1));
	TestFalse(TEXT("Conn1 back in base disallowed for X+Y"), Backend.IsAllowed(GroupXY, Conn1));

	// A new connection arriving later gets statuses for existing groups.
	Table.SetConnectionMembership(3, { X, Y });
	TestTrue(TEXT("Late connection allowed for X"), Backend.IsAllowed(GroupX, 3));
	TestTrue(TEXT("Late connection allowed for X+Y"), Backend.IsAllowed(GroupXY, 3));

	Table.RemoveConnection(3);
	TestEqual(TEXT("Connection removed"), Table.GetConnectionCount(), 2);
	TestFalse(TEXT("Unknown connection is not allowed"), Table.IsActorAllowedForConnection(InX, 3));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingIrisPendingTest,
	"ActorPhasing.Iris.PendingActorRetriesUntilReplicating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingIrisPendingTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingIrisPending"));
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);

	FRecordingBackend Backend;
	FActorPhasingIrisGroupTable Table(Backend);

	AActor* Late = World->SpawnActor<AActor>();
	Table.SetActorKeys(Late, { X });
	TestEqual(TEXT("Not replicating yet -> pending"), Table.GetPendingCount(), 1);
	TestFalse(TEXT("Not grouped while pending"), Table.IsActorGrouped(Late));
	TestEqual(TEXT("Group created ahead of time"), Table.GetGroupCount(), 1);

	Table.FlushPending();
	TestEqual(TEXT("Still pending without a handle"), Table.GetPendingCount(), 1);

	Backend.ReplicatingActors.Add(Late);
	Table.FlushPending();
	TestEqual(TEXT("Filed once replicating"), Table.GetPendingCount(), 0);
	TestTrue(TEXT("Grouped after flush"), Table.IsActorGrouped(Late));

	// A key change while pending re-targets the pending actor.
	AActor* Moved = World->SpawnActor<AActor>();
	Table.SetActorKeys(Moved, { X });
	Table.SetActorKeys(Moved, { Y });
	Backend.ReplicatingActors.Add(Moved);
	Table.FlushPending();
	TestTrue(TEXT("Moved actor filed under its final set"), Table.IsActorGrouped(Moved));
	TestTrue(TEXT("Moved actor is not in group X"), Table.FindGroupIdForActor(Moved) != Table.FindGroupIdForActor(Late));
	TestEqual(TEXT("Groups: X and Y"), Table.GetGroupCount(), 2);

	// Group creation failures keep actors pending and are retried. The table reports the failure once.
	AddExpectedError(TEXT("could not create a filter group"), EAutomationExpectedErrorFlags::Contains, 1);
	Backend.bFailCreate = true;
	AActor* Unlucky = World->SpawnActor<AActor>();
	Backend.ReplicatingActors.Add(Unlucky);
	Table.SetActorKeys(Unlucky, { Key(TEXT("Test"), 9) });
	TestEqual(TEXT("Failed group -> pending"), Table.GetPendingCount(), 1);
	Backend.bFailCreate = false;
	Table.FlushPending();
	TestTrue(TEXT("Filed after the backend recovers"), Table.IsActorGrouped(Unlucky));

	// A destroyed actor is dropped on flush. (Bare test worlds have no world context, so mark the
	// actor garbage directly instead of going through UWorld::DestroyActor.)
	AActor* Doomed = World->SpawnActor<AActor>();
	Table.SetActorKeys(Doomed, { X });
	TestEqual(TEXT("Doomed actor pending"), Table.GetPendingCount(), 1);
	Doomed->MarkAsGarbage();
	Table.FlushPending();
	TestEqual(TEXT("Destroyed pending actor dropped"), Table.GetPendingCount(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingIrisObservationTest,
	"ActorPhasing.Iris.ObservedPhasesAllowObservableGroupsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingIrisObservationTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingIrisObserve"));
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const uint32 Observer = 1;

	FRecordingBackend Backend;
	FActorPhasingIrisGroupTable Table(Backend);

	AActor* Prop = World->SpawnActor<AActor>();
	AActor* Pawn = World->SpawnActor<AActor>();
	Backend.ReplicatingActors.Add(Prop);
	Backend.ReplicatingActors.Add(Pawn);

	Table.SetActorKeys(Prop, { X }, /*bVisibleToObservers*/ true);
	Table.SetActorKeys(Pawn, { X }, /*bVisibleToObservers*/ false);
	TestNotEqual(TEXT("Observable and member-only actors use different groups"), Table.FindGroupIdForActor(Prop), Table.FindGroupIdForActor(Pawn));

	Table.SetConnectionView(Observer, { FActorPhaseKey::Base() }, { X });
	TestTrue(TEXT("Observer receives the observable prop"), Backend.IsAllowed(Table.FindGroupIdForActor(Prop), Observer));
	TestFalse(TEXT("Observer never receives member-only actors"), Backend.IsAllowed(Table.FindGroupIdForActor(Pawn), Observer));
	TestTrue(TEXT("Query agrees for the prop"), Table.IsActorAllowedForConnection(Prop, Observer));

	// A membership update keeps the connection's observed phases.
	Table.SetConnectionMembership(Observer, { FActorPhaseKey::Base() });
	TestTrue(TEXT("Membership-only update keeps observation"), Table.IsActorAllowedForConnection(Prop, Observer));

	Table.SetConnectionView(Observer, { FActorPhaseKey::Base() }, {});
	TestFalse(TEXT("Stopping observation disallows the prop"), Backend.IsAllowed(Table.FindGroupIdForActor(Prop), Observer));

	// Toggling observability re-files the actor into the other group.
	Table.SetActorKeys(Prop, { X }, /*bVisibleToObservers*/ false);
	TestEqual(TEXT("Member-only prop shares the pawn's group"), Table.FindGroupIdForActor(Prop), Table.FindGroupIdForActor(Pawn));

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
