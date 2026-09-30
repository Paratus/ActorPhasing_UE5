// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ActorPhasingTestFixtures.h"

#include "ActorPhaseSubsystem.h"
#include "ActorPhaseVolume.h"
#include "ActorPhasingLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "CoreGlobals.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

using namespace ActorPhasingTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingPawnMirrorTest,
	"ActorPhasing.Membership.PawnKeysFollowOwnerOnPawnSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingPawnMirrorTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingPawnMirror"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FTestPlayer A = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("Fixture is complete"), A.IsValid())) return false;

	UActorPhaseComponent* PawnPhase = A.Pawn->FindComponentByClass<UActorPhaseComponent>();
	if (!TestNotNull(TEXT("Possession added a phase component to the pawn"), PawnPhase)) return false;
	TestTrue(TEXT("The pawn component mirrors its owner"), PawnPhase->MirrorsOwnerMembership());
	TestTrue(TEXT("A fresh pawn is in the base phase"), PawnPhase->HasPhaseKey(FActorPhaseKey::Base()));

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	A.Membership->SetSinglePhase(X);
	TestTrue(TEXT("Membership change reaches the pawn"), PawnPhase->HasPhaseKey(X));
	TestFalse(TEXT("The pawn left the base phase"), PawnPhase->HasPhaseKey(FActorPhaseKey::Base()));

	// A new pawn possessed later inherits the current membership.
	APawn* NewPawn = World->SpawnActor<APawn>();
	NewPawn->PossessedBy(A.Controller);
	A.Controller->SetPawn(NewPawn);
	UActorPhaseComponent* NewPawnPhase = NewPawn->FindComponentByClass<UActorPhaseComponent>();
	if (!TestNotNull(TEXT("The new pawn received a phase component"), NewPawnPhase)) return false;
	TestTrue(TEXT("The new pawn is in the owner's phase"), NewPawnPhase->HasPhaseKey(X));
	TestTrue(TEXT("The previous pawn keeps its last phase"), PawnPhase->HasPhaseKey(X));
	TestEqual(TEXT("GetViewedPawn follows possession"), A.Membership->GetViewedPawn(), NewPawn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingVolumeEnterExitTest,
	"ActorPhasing.Volume.EnterAppliesResolvedKeyExitRestoresBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingVolumeEnterExitTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingVolume"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FActorPhaseKey X = Key(TEXT("Volume"), 42);
	AActorPhaseVolume* Volume = World->SpawnActor<AActorPhaseVolume>(AActorPhaseVolume::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("Volume exists"), Volume)) return false;
	Volume->SetFixedPhase(X);

	// Spawn outside, possess, then walk in: the volume needs the PlayerState at entry time.
	const FTestPlayer A = SpawnPlayer(World, FVector(5000.0f, 0.0f, 0.0f), /*bCollidingPawn*/ true);
	if (!TestTrue(TEXT("Fixture is complete"), A.IsValid())) return false;
	TestTrue(TEXT("Outside the volume the player is in base"), A.Membership->IsInBasePhase());

	A.Pawn->SetActorLocation(FVector::ZeroVector);
	TestTrue(TEXT("The pawn overlaps the volume"), Volume->IsPawnInside(A.Pawn));
	TestTrue(TEXT("Entering applies the resolved phase"), A.Membership->HasPhase(X));
	TestFalse(TEXT("Entering leaves the base phase"), A.Membership->HasPhase(FActorPhaseKey::Base()));

	TArray<APlayerState*> Occupants;
	Volume->GetOccupants(Occupants);
	TestEqual(TEXT("The player is an occupant"), Occupants.Num(), 1);

	// A furniture piece in phase X placed in the yard is visible now.
	AActor* Furniture = SpawnPhasedActor(World, { X }, FVector(100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Phase X furniture is visible inside"), UActorPhasingLibrary::IsActorRelevantToViewer(Furniture, A.Controller));

	A.Pawn->SetActorLocation(FVector(5000.0f, 0.0f, 0.0f));
	TestFalse(TEXT("The pawn left the volume"), Volume->IsPawnInside(A.Pawn));
	TestTrue(TEXT("Leaving restores the base phase"), A.Membership->IsInBasePhase());
	TestFalse(TEXT("Phase X furniture is hidden outside"), UActorPhasingLibrary::IsActorRelevantToViewer(Furniture, A.Controller));
	Volume->GetOccupants(Occupants);
	TestEqual(TEXT("No occupants remain"), Occupants.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingVolumeSpawnInsideTest,
	"ActorPhasing.Volume.SpawnInsideRetriesUntilPlayerState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingVolumeSpawnInsideTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingSpawnInside"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FActorPhaseKey X = Key(TEXT("Volume"), 1);
	AActorPhaseVolume* Volume = World->SpawnActor<AActorPhaseVolume>(AActorPhaseVolume::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("Volume exists"), Volume)) return false;
	Volume->SetFixedPhase(X);

	// A pawn that exists inside the box before it has a PlayerState (spawn-inside case).
	APawn* Pawn = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform::Identity);
	UBoxComponent* Root = NewObject<UBoxComponent>(Pawn, TEXT("Root"));
	Root->SetBoxExtent(FVector(40.0f, 40.0f, 90.0f));
	Root->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Root->SetCollisionObjectType(ECC_Pawn);
	Root->SetCollisionResponseToAllChannels(ECR_Ignore);
	Root->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	Root->SetGenerateOverlapEvents(true);
	Pawn->SetRootComponent(Root);
	Root->RegisterComponent();
	Pawn->SetActorLocation(FVector::ZeroVector);
	// A pawn assembled after spawning never moved, so refresh overlaps the way a normal spawn does.
	Pawn->UpdateOverlaps(true);
	TestTrue(TEXT("The pawn overlaps the volume"), Volume->IsPawnInside(Pawn));

	TArray<APlayerState*> Occupants;
	Volume->GetOccupants(Occupants);
	TestEqual(TEXT("No PlayerState yet: not an occupant"), Occupants.Num(), 0);

	// Possession arrives later; the retry timer picks the pawn up.
	APlayerState* PlayerState = World->SpawnActor<APlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	UActorPhaseMembershipComponent* Membership = NewObject<UActorPhaseMembershipComponent>(PlayerState, UActorPhaseMembershipComponent::StaticClass(), TEXT("ActorPhaseMembership"));
	Membership->RegisterComponent();
	Controller->PlayerState = PlayerState;
	PlayerState->SetOwner(Controller);
	Pawn->PossessedBy(Controller);
	Controller->SetPawn(Pawn);

	// Timer manager activates pending timers on its first tick; each later tick needs a new frame.
	++GFrameCounter;
	World->GetTimerManager().Tick(UE_KINDA_SMALL_NUMBER);
	++GFrameCounter;
	World->GetTimerManager().Tick(2.1f);

	TestTrue(TEXT("The retry applied the phase once the PlayerState existed"), Membership->HasPhase(X));
	Volume->GetOccupants(Occupants);
	TestEqual(TEXT("The player became an occupant"), Occupants.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingMoveIgnoreTest,
	"ActorPhasing.Subsystem.DisjointPawnsIgnoreEachOtherWhenMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingMoveIgnoreTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingMoveIgnore"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	UActorPhaseSubsystem* Subsystem = World->GetSubsystem<UActorPhaseSubsystem>();
	if (!TestNotNull(TEXT("Subsystem exists"), Subsystem)) return false;

	const FTestPlayer A = SpawnPlayer(World, FVector::ZeroVector, true);
	const FTestPlayer B = SpawnPlayer(World, FVector(100.0f, 0.0f, 0.0f), true);
	if (!TestTrue(TEXT("Fixtures are complete"), A.IsValid() && B.IsValid())) return false;
	UPrimitiveComponent* RootA = Cast<UPrimitiveComponent>(A.Pawn->GetRootComponent());
	UPrimitiveComponent* RootB = Cast<UPrimitiveComponent>(B.Pawn->GetRootComponent());

	// Any live object works as a scope id; a plain actor stands in for the phase volume here.
	AActor* Scope = World->SpawnActor<AActor>();
	Subsystem->RegisterPawnInCollisionScope(Scope, A.Pawn);
	Subsystem->RegisterPawnInCollisionScope(Scope, B.Pawn);
	TestFalse(TEXT("Same phase: A does not ignore B"), RootA->GetMoveIgnoreActors().Contains(B.Pawn));

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FActorPhaseKey Y = Key(TEXT("Test"), 2);
	A.Membership->SetSinglePhase(X);
	B.Membership->SetSinglePhase(Y);
	TestTrue(TEXT("Different phases: A ignores B when moving"), RootA->GetMoveIgnoreActors().Contains(B.Pawn));
	TestTrue(TEXT("Different phases: B ignores A when moving"), RootB->GetMoveIgnoreActors().Contains(A.Pawn));

	AActor* Furniture = SpawnPhasedActor(World, { Y }, FVector(50.0f, 0.0f, 0.0f));
	Furniture->FindComponentByClass<UActorPhaseComponent>()->SetCollisionScope(Scope);
	TestTrue(TEXT("A ignores phase Y furniture"), RootA->GetMoveIgnoreActors().Contains(Furniture));
	TestFalse(TEXT("B collides with phase Y furniture"), RootB->GetMoveIgnoreActors().Contains(Furniture));

	B.Membership->SetSinglePhase(X);
	TestFalse(TEXT("Rejoining the phase restores pawn collision"), RootA->GetMoveIgnoreActors().Contains(B.Pawn));
	TestTrue(TEXT("B now ignores phase Y furniture"), RootB->GetMoveIgnoreActors().Contains(Furniture));

	Subsystem->UnregisterPawnFromCollisionScope(Scope, A.Pawn);
	TestFalse(TEXT("Leaving the scope clears A's ignores"), RootA->GetMoveIgnoreActors().Contains(Furniture));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingIdleRuleTest,
	"ActorPhasing.Subsystem.UnviewedPhaseFiresAfterGrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingIdleRuleTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingIdle"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	UActorPhaseSubsystem* Subsystem = World->GetSubsystem<UActorPhaseSubsystem>();
	if (!TestNotNull(TEXT("Subsystem exists"), Subsystem)) return false;

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	const FTestPlayer A = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("Fixture is complete"), A.IsValid())) return false;
	A.Membership->SetSinglePhase(X);

	Subsystem->RegisterIdleRule(X, 2.0f);
	TestTrue(TEXT("Rule registered"), Subsystem->HasIdleRule(X));

	Subsystem->Tick(5.0f);
	TestTrue(TEXT("A viewed phase never fires"), Subsystem->HasIdleRule(X));

	A.Membership->ResetToBase();
	Subsystem->Tick(1.0f);
	TestTrue(TEXT("Grace not yet elapsed"), Subsystem->HasIdleRule(X));

	A.Membership->SetSinglePhase(X);
	Subsystem->Tick(1.0f);
	A.Membership->ResetToBase();
	Subsystem->Tick(1.0f);
	TestTrue(TEXT("A returning viewer resets the grace"), Subsystem->HasIdleRule(X));

	Subsystem->Tick(1.0f);
	TestFalse(TEXT("The rule fires and is consumed once the grace elapses"), Subsystem->HasIdleRule(X));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingInteractionRejectTest,
	"ActorPhasing.Subsystem.CrossPhaseInteractionRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingInteractionRejectTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld(TEXT("ActorPhasingInteraction"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	ON_SCOPE_EXIT { DestroyTestWorld(World); };

	const FTestPlayer A = SpawnPlayer(World, FVector::ZeroVector);
	const FTestPlayer B = SpawnPlayer(World, FVector(100.0f, 0.0f, 0.0f));
	if (!TestTrue(TEXT("Fixtures are complete"), A.IsValid() && B.IsValid())) return false;

	const FActorPhaseKey X = Key(TEXT("Test"), 1);
	A.Membership->SetSinglePhase(X);
	B.Membership->SetSinglePhase(Key(TEXT("Test"), 2));

	AActor* Chair = SpawnPhasedActor(World, { X });
	TestTrue(TEXT("A viewer in the chair's phase may interact"), UActorPhasingLibrary::CanViewerInteractWithActor(A.Controller, Chair));
	TestTrue(TEXT("The pawn works as the viewer too"), UActorPhasingLibrary::CanViewerInteractWithActor(A.Pawn, Chair));
	TestFalse(TEXT("A viewer in another phase is rejected"), UActorPhasingLibrary::CanViewerInteractWithActor(B.Controller, Chair));
	TestFalse(TEXT("Cross-phase players cannot target each other"), UActorPhasingLibrary::CanViewerInteractWithActor(B.Controller, A.Pawn));

	AActor* Unphased = World->SpawnActor<AActor>();
	TestTrue(TEXT("Unphased world actors are always reachable"), UActorPhasingLibrary::CanViewerInteractWithActor(B.Controller, Unphased));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorPhasingVolumeCollisionIdiomTest,
	"ActorPhasing.Volume.TriggerCollisionIdiom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPhasingVolumeCollisionIdiomTest::RunTest(const FString& Parameters)
{
	const AActorPhaseVolume* Defaults = GetDefault<AActorPhaseVolume>();
	const UBoxComponent* Box = Defaults ? Defaults->GetVolumeBounds() : nullptr;
	if (!TestNotNull(TEXT("The volume has a box"), Box)) return false;

	TestEqual(TEXT("Object type is WorldDynamic (never answers world-geometry queries)"), Box->GetCollisionObjectType(), ECC_WorldDynamic);
	TestEqual(TEXT("Query only"), Box->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("Pawns overlap"), Box->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
	for (int32 ChannelIndex = 0; ChannelIndex < ECC_MAX; ++ChannelIndex)
	{
		const ECollisionChannel Channel = static_cast<ECollisionChannel>(ChannelIndex);
		TestNotEqual(FString::Printf(TEXT("Channel %d never blocks"), ChannelIndex), Box->GetCollisionResponseToChannel(Channel), ECR_Block);
	}
	TestFalse(TEXT("Volumes do not replicate"), Defaults->GetIsReplicated());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
