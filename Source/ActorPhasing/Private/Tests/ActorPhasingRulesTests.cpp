// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ActorPhasingTestFixtures.h"

#include "ActorPhaseSubsystem.h"
#include "ActorPhasingLibrary.h"
#include "Misc/ScopeExit.h"

using namespace ActorPhasingTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingKeysIntersectTest,
	"ActorPhasing.Rules.KeysIntersect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingKeysIntersectTest::RunTest(const FString& Parameters)
{
	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);
	const FActorPhaseKey OtherDomainX = Key(TEXT("Other"), 1);

	TestTrue(TEXT("Shared key intersects"), UActorPhasingLibrary::KeysIntersect({ X, Y }, { Y }));
	TestFalse(TEXT("Disjoint keys do not intersect"), UActorPhasingLibrary::KeysIntersect({ X }, { Y }));
	TestFalse(TEXT("Same id in another domain is a different phase"), UActorPhasingLibrary::KeysIntersect({ X }, { OtherDomainX }));
	TestFalse(TEXT("Empty sets never intersect"), UActorPhasingLibrary::KeysIntersect({}, { X }));
	TestFalse(TEXT("Invalid keys are ignored"), UActorPhasingLibrary::KeysIntersect({ FActorPhaseKey() }, { FActorPhaseKey() }));
	TestTrue(TEXT("Base intersects base"), UActorPhasingLibrary::KeysIntersect({ FActorPhaseKey::Base() }, UActorPhasingLibrary::GetBaseMembership()));
	TestEqual(TEXT("Key prints as Domain:Id"), X.ToString(), FString(TEXT("Test:1")));
	TestEqual(TEXT("Invalid key prints None"), FActorPhaseKey().ToString(), FString(TEXT("None")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingUnphasedRelevantTest,
	"ActorPhasing.Rules.UnphasedActorRelevantToEveryone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingUnphasedRelevantTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingUnphased"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FTestPlayer Viewer = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("Viewer fixture is complete"), Viewer.IsValid())) return false;
	Viewer.Membership->SetSinglePhase(Key(TEXT("Test"), 7));

	AActor* NoComponent = World->SpawnActor<AActor>();
	AActor* EmptyKeys = SpawnPhasedActor(World, {});

	TestTrue(TEXT("Actor without a phase component is relevant to a phased viewer"), UActorPhasingLibrary::IsActorRelevantToViewer(NoComponent, Viewer.Controller));
	TestTrue(TEXT("Actor with an empty key set is relevant to a phased viewer"), UActorPhasingLibrary::IsActorRelevantToViewer(EmptyKeys, Viewer.Controller));

	TArray<FActorPhaseKey> Effective;
	TestFalse(TEXT("Empty key set reports every phase"), UActorPhasingLibrary::GetEffectivePhaseKeys(EmptyKeys, Effective));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingSymmetricTest,
	"ActorPhasing.Rules.IntersectionIsSymmetric",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingSymmetricTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingSymmetric"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FTestPlayer A = SpawnPlayer(World, FVector(0.0f, 0.0f, 0.0f));
	const FTestPlayer B = SpawnPlayer(World, FVector(100.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("Both fixtures are complete"), A.IsValid() && B.IsValid())) return false;

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);
	A.Membership->SetSinglePhase(X);
	B.Membership->SetSinglePhase(Y);

	TestFalse(TEXT("A's pawn is hidden from B"), UActorPhasingLibrary::IsActorRelevantToViewer(A.Pawn, B.Controller));
	TestFalse(TEXT("B's pawn is hidden from A"), UActorPhasingLibrary::IsActorRelevantToViewer(B.Pawn, A.Controller));
	TestFalse(TEXT("Pawns of different phases are incompatible"), UActorPhasingLibrary::AreActorsInCompatiblePhases(A.Pawn, B.Pawn));

	B.Membership->SetSinglePhase(X);
	TestTrue(TEXT("Same phase: A's pawn is visible to B"), UActorPhasingLibrary::IsActorRelevantToViewer(A.Pawn, B.Controller));
	TestTrue(TEXT("Same phase: B's pawn is visible to A"), UActorPhasingLibrary::IsActorRelevantToViewer(B.Pawn, A.Controller));
	TestTrue(TEXT("Pawns of the same phase are compatible"), UActorPhasingLibrary::AreActorsInCompatiblePhases(A.Pawn, B.Pawn));

	AActor* Furniture = SpawnPhasedActor(World, { X });
	TestTrue(TEXT("Phase X furniture is visible to X viewers"), UActorPhasingLibrary::IsActorRelevantToViewer(Furniture, A.Controller));
	B.Membership->SetSinglePhase(Y);
	TestFalse(TEXT("Phase X furniture is hidden from Y viewers"), UActorPhasingLibrary::IsActorRelevantToViewer(Furniture, B.Controller));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingOwnerRelevantTest,
	"ActorPhasing.Rules.OwnerConnectionAlwaysRelevant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingOwnerRelevantTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingOwner"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FTestPlayer A = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("Fixture is complete"), A.IsValid())) return false;
	A.Membership->SetSinglePhase(Key(TEXT("Test"), 1));

	TestTrue(TEXT("A viewer always sees its own pawn"), UActorPhasingLibrary::IsActorRelevantToViewer(A.Pawn, A.Controller));
	TestTrue(TEXT("A PlayerState viewer sees its pawn"), UActorPhasingLibrary::IsActorRelevantToViewer(A.Pawn, A.PlayerState));

	AActor* Owned = SpawnPhasedActor(World, { Key(TEXT("Test"), 99) });
	Owned->SetOwner(A.Pawn);
	TestTrue(TEXT("An owned actor is relevant to its owner whatever its phase"), UActorPhasingLibrary::IsActorRelevantToViewer(Owned, A.Controller));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingReplaceBaseTest,
	"ActorPhasing.Rules.SetMembershipReplacesBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingReplaceBaseTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingReplaceBase"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FTestPlayer A = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("Fixture is complete"), A.IsValid())) return false;

	TestTrue(TEXT("A fresh membership is the base phase"), A.Membership->IsInBasePhase());

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	A.Membership->SetSinglePhase(X);
	TestFalse(TEXT("Selecting a phase leaves the base phase"), A.Membership->HasPhase(FActorPhaseKey::Base()));
	TestEqual(TEXT("Membership holds exactly the selected phase"), A.Membership->GetMembership().Num(), 1);

	AActor* BaseActor = SpawnPhasedActor(World, { FActorPhaseKey::Base() });
	TestFalse(TEXT("Base-phase actors are hidden from a phased viewer"), UActorPhasingLibrary::IsActorRelevantToViewer(BaseActor, A.Controller));

	A.Membership->SetMembership({});
	TestTrue(TEXT("An empty membership falls back to base"), A.Membership->IsInBasePhase());
	TestTrue(TEXT("Back in base the base actor is visible"), UActorPhasingLibrary::IsActorRelevantToViewer(BaseActor, A.Controller));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingBaseViewerTest,
	"ActorPhasing.Rules.BaseViewerCannotSeePhasedPawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingBaseViewerTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingBaseViewer"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FTestPlayer Phased = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("Phased fixture is complete"), Phased.IsValid())) return false;

	// A viewer that never received a membership component: treated as the base phase.
	APlayerController* Outsider = World->SpawnActor<APlayerController>();
	APlayerState* OutsiderState = World->SpawnActor<APlayerState>();
	Outsider->PlayerState = OutsiderState;
	OutsiderState->SetOwner(Outsider);

	TestTrue(TEXT("Before phasing the outsider sees the pawn"), UActorPhasingLibrary::IsActorRelevantToViewer(Phased.Pawn, Outsider));

	Phased.Membership->SetSinglePhase(Key(TEXT("Test"), 1));
	TestFalse(TEXT("A base viewer cannot see a phased pawn"), UActorPhasingLibrary::IsActorRelevantToViewer(Phased.Pawn, Outsider));
	TestFalse(TEXT("The phased viewer cannot interact with base actors"), UActorPhasingLibrary::CanViewerInteractWithActor(Phased.Controller, SpawnPhasedActor(World, { FActorPhaseKey::Base() })));

	Phased.Membership->ResetToBase();
	TestTrue(TEXT("Back in base the outsider sees the pawn again"), UActorPhasingLibrary::IsActorRelevantToViewer(Phased.Pawn, Outsider));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingOwnerChainTest,
	"ActorPhasing.Rules.OwnerChainInheritance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingOwnerChainTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingOwnerChain"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FTestPlayer A = SpawnPlayer(World, FVector::ZeroVector);
	const FTestPlayer B = SpawnPlayer(World, FVector(100.0f, 0.0f, 0.0f));
	const FTestPlayer C = SpawnPlayer(World, FVector(200.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("Fixtures are complete"), A.IsValid() && B.IsValid() && C.IsValid())) return false;

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	A.Membership->SetSinglePhase(X);
	B.Membership->SetSinglePhase(Key(TEXT("Test"), 2));
	C.Membership->SetSinglePhase(X);

	// A projectile with no phase component of its own, owned by A's pawn.
	AActor* Projectile = World->SpawnActor<AActor>();
	Projectile->SetOwner(A.Pawn);

	TArray<FActorPhaseKey> Effective;
	TestTrue(TEXT("Owned actor inherits its owner's keys"), UActorPhasingLibrary::GetEffectivePhaseKeys(Projectile, Effective));
	TestTrue(TEXT("Inherited key is the owner's phase"), Effective.Contains(X));
	TestTrue(TEXT("Visible to a viewer in the owner's phase"), UActorPhasingLibrary::IsActorRelevantToViewer(Projectile, C.Controller));
	TestFalse(TEXT("Hidden from a viewer in another phase"), UActorPhasingLibrary::IsActorRelevantToViewer(Projectile, B.Controller));
	TestTrue(TEXT("Always visible to its owner"), UActorPhasingLibrary::IsActorRelevantToViewer(Projectile, A.Controller));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingRegistryCountsTest,
	"ActorPhasing.Rules.SubsystemCountsViewersAndActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingRegistryCountsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingCounts"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	UActorPhaseSubsystem* Subsystem = World->GetSubsystem<UActorPhaseSubsystem>();
	if (!TestNotNull(TEXT("Subsystem exists in a game world"), Subsystem)) return false;

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FTestPlayer A = SpawnPlayer(World, FVector::ZeroVector);
	const FTestPlayer B = SpawnPlayer(World, FVector(100.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("Fixtures are complete"), A.IsValid() && B.IsValid())) return false;

	TestEqual(TEXT("Nobody views X yet"), Subsystem->GetViewerCount(X), 0);
	A.Membership->SetSinglePhase(X);
	B.Membership->SetSinglePhase(X);
	TestEqual(TEXT("Two viewers in X"), Subsystem->GetViewerCount(X), 2);
	TestEqual(TEXT("Two mirrored pawns in X"), Subsystem->GetActorCount(X), 2);

	AActor* Furniture = SpawnPhasedActor(World, { X });
	TestEqual(TEXT("Furniture joins the X registry"), Subsystem->GetActorCount(X), 3);

	TArray<APlayerState*> Viewers;
	Subsystem->GetViewersInPhase(X, Viewers);
	TestEqual(TEXT("Viewer query lists both PlayerStates"), Viewers.Num(), 2);

	B.Membership->ResetToBase();
	TestEqual(TEXT("Leaving X drops the count"), Subsystem->GetViewerCount(X), 1);

	Furniture->Destroy();
	TestEqual(TEXT("Destroyed furniture leaves the registry"), Subsystem->GetActorCount(X), 1);

	TestTrue(TEXT("DescribeState mentions the phase"), Subsystem->DescribeState().Contains(X.ToString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingObservationRevealsContentTest,
	"ActorPhasing.Rules.ObservationRevealsWorldContentOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingObservationRevealsContentTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingObserve"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FTestPlayer Member = SpawnPlayer(World, FVector::ZeroVector);
	const FTestPlayer Observer = SpawnPlayer(World, FVector(5000.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("Fixtures are complete"), Member.IsValid() && Observer.IsValid())) return false;

	Member.Membership->SetSinglePhase(X);
	AActor* Furniture = SpawnPhasedActor(World, { X });
	AActor* Secret = SpawnPhasedActor(World, { X });
	Secret->FindComponentByClass<UActorPhaseComponent>()->SetVisibleToObservers(false);

	TestFalse(TEXT("Before observing, X content is hidden from a base viewer"), UActorPhasingLibrary::IsActorRelevantToViewer(Furniture, Observer.Controller));

	Observer.Membership->AddObservedPhase(X);
	TestTrue(TEXT("Observer is still a base-phase member"), Observer.Membership->IsInBasePhase());
	TestTrue(TEXT("Observed world content is visible"), UActorPhasingLibrary::IsActorRelevantToViewer(Furniture, Observer.Controller));
	TestFalse(TEXT("Content hidden from observers stays hidden"), UActorPhasingLibrary::IsActorRelevantToViewer(Secret, Observer.Controller));
	TestFalse(TEXT("A member's pawn is never shown to observers"), UActorPhasingLibrary::IsActorRelevantToViewer(Member.Pawn, Observer.Controller));
	TestFalse(TEXT("Members still cannot see the observer's base pawn"), UActorPhasingLibrary::IsActorRelevantToViewer(Observer.Pawn, Member.Controller));
	TestFalse(TEXT("Observers cannot interact with observed content"), UActorPhasingLibrary::CanViewerInteractWithActor(Observer.Controller, Furniture));
	TestFalse(TEXT("Observed content is not in the observer's phase"), UActorPhasingLibrary::IsActorInViewerPhase(Furniture, Observer.Controller));
	TestTrue(TEXT("Members can interact with their phase's content"), UActorPhasingLibrary::CanViewerInteractWithActor(Member.Controller, Furniture));

	TArray<FActorPhaseKey> PawnKeys;
	UActorPhasingLibrary::GetEffectivePhaseKeys(Observer.Pawn, PawnKeys);
	TestFalse(TEXT("Observation never reaches the pawn"), PawnKeys.Contains(X));

	bool bVisibleToObservers = false;
	TArray<FActorPhaseKey> Keys;
	TestTrue(TEXT("Furniture reports effective phase info"), UActorPhasingLibrary::GetEffectivePhaseInfo(Furniture, Keys, bVisibleToObservers));
	TestTrue(TEXT("Furniture is visible to observers by default"), bVisibleToObservers);
	UActorPhasingLibrary::GetEffectivePhaseInfo(Member.Pawn, Keys, bVisibleToObservers);
	TestFalse(TEXT("Mirrored pawns are never visible to observers"), bVisibleToObservers);

	Observer.Membership->RemoveObservedPhase(X);
	TestFalse(TEXT("Stopping observation hides the content again"), UActorPhasingLibrary::IsActorRelevantToViewer(Furniture, Observer.Controller));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingObserverCountsTest,
	"ActorPhasing.Rules.ObserversCountSeparatelyAndKeepIdleRulesArmed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingObserverCountsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingObserverCounts"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	UActorPhaseSubsystem* Subsystem = World->GetSubsystem<UActorPhaseSubsystem>();
	if (!TestNotNull(TEXT("Subsystem exists in a game world"), Subsystem)) return false;

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FTestPlayer Observer = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("Fixture is complete"), Observer.IsValid())) return false;
	SpawnPhasedActor(World, { X });

	Observer.Membership->SetObservedPhases({ X, X, FActorPhaseKey() });
	TestEqual(TEXT("Observed phases are sanitized"), Observer.Membership->GetObservedPhases().Num(), 1);
	TestEqual(TEXT("One observer of X"), Subsystem->GetObserverCount(X), 1);
	TestEqual(TEXT("Observers are not members"), Subsystem->GetViewerCount(X), 0);
	TestTrue(TEXT("DescribeState lists the observation"), Subsystem->DescribeState().Contains(TEXT("observing")));

	Subsystem->RegisterIdleRule(X, 2.0f);
	Subsystem->Tick(5.0f);
	TestTrue(TEXT("An observed phase is not idle"), Subsystem->HasIdleRule(X));

	Observer.Membership->ClearObservedPhases();
	TestEqual(TEXT("Observer count drops"), Subsystem->GetObserverCount(X), 0);
	Subsystem->Tick(1.0f);
	Subsystem->Tick(1.5f);
	TestFalse(TEXT("Once unobserved, the idle rule fires"), Subsystem->HasIdleRule(X));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
