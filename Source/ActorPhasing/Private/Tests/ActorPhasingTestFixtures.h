// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "ActorPhaseComponent.h"
#include "ActorPhaseKey.h"
#include "ActorPhaseMembershipComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"

/** Shared helpers for the Actor Phasing automation tests. */
namespace ActorPhasingTests
{
	/** A bare game world with initialized actors, so spawned actors begin play and overlaps resolve. */
	inline UWorld* CreateTestWorld(const TCHAR* Name)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
		if (World)
		{
			World->InitializeActorsForPlay(FURL());
			World->SetBegunPlay(true);
		}
		return World;
	}

	inline void DestroyTestWorld(UWorld* World)
	{
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			// A destroyed world keeps its render scene until the next GC; collecting here keeps a
			// long test run from accumulating GPU scenes.
			CollectGarbage(RF_NoFlags);
		}
	}

	/** A possessed player: controller, PlayerState with a membership component, and a pawn. */
	struct FTestPlayer
	{
		APlayerController* Controller = nullptr;
		APlayerState* PlayerState = nullptr;
		APawn* Pawn = nullptr;
		UActorPhaseMembershipComponent* Membership = nullptr;

		bool IsValid() const { return Controller && PlayerState && Pawn && Membership; }
	};

	/**
	 * Spawns a possessed player. With bCollidingPawn the pawn's root is a Pawn-typed box that
	 * overlaps WorldDynamic triggers, so phase volumes see it.
	 */
	inline FTestPlayer SpawnPlayer(UWorld* World, const FVector& Location, const bool bCollidingPawn = false)
	{
		FTestPlayer Player;
		Player.PlayerState = World->SpawnActor<APlayerState>();
		Player.Controller = World->SpawnActor<APlayerController>();
		Player.Pawn = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform(Location));
		if (!Player.PlayerState || !Player.Controller || !Player.Pawn)
		{
			return Player;
		}

		if (bCollidingPawn)
		{
			UBoxComponent* Root = NewObject<UBoxComponent>(Player.Pawn, TEXT("Root"));
			Root->SetBoxExtent(FVector(40.0f, 40.0f, 90.0f));
			Root->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Root->SetCollisionObjectType(ECC_Pawn);
			Root->SetCollisionResponseToAllChannels(ECR_Ignore);
			Root->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
			Root->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			Root->SetGenerateOverlapEvents(true);
			Player.Pawn->SetRootComponent(Root);
			Root->RegisterComponent();
		}
		else
		{
			USceneComponent* Root = NewObject<USceneComponent>(Player.Pawn, TEXT("Root"));
			Player.Pawn->SetRootComponent(Root);
			Root->RegisterComponent();
		}
		Player.Pawn->SetActorLocation(Location);

		Player.Membership = NewObject<UActorPhaseMembershipComponent>(Player.PlayerState, UActorPhaseMembershipComponent::StaticClass(), TEXT("ActorPhaseMembership"));
		Player.Membership->RegisterComponent();

		// The real chain without Possess(): avoids ClientRestart's input and camera setup.
		Player.Controller->PlayerState = Player.PlayerState;
		Player.PlayerState->SetOwner(Player.Controller);
		Player.Pawn->PossessedBy(Player.Controller);
		Player.Controller->SetPawn(Player.Pawn);
		return Player;
	}

	/** A replicated-style actor with a phase component holding the given keys (empty = every phase). */
	inline AActor* SpawnPhasedActor(UWorld* World, const TArray<FActorPhaseKey>& Keys, const FVector& Location = FVector::ZeroVector)
	{
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location));
		if (!Actor)
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);

		UActorPhaseComponent* Phase = NewObject<UActorPhaseComponent>(Actor, UActorPhaseComponent::StaticClass(), TEXT("ActorPhase"));
		Phase->RegisterComponent();
		Phase->SetPhaseKeys(Keys);
		return Actor;
	}

	inline FActorPhaseKey Key(const TCHAR* Domain, const int64 Id)
	{
		return FActorPhaseKey::Make(FName(Domain), Id);
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
