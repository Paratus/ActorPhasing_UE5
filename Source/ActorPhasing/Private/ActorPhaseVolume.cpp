// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhaseVolume.h"

#include "ActorPhaseMembershipComponent.h"
#include "ActorPhaseSubsystem.h"
#include "ActorPhasingModule.h"
#include "ActorPhasingSettings.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

AActorPhaseVolume::AActorPhaseVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // level-placed; exists on server and clients from map load
	SetCanBeDamaged(false);

	VolumeBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("VolumeBounds"));
	SetRootComponent(VolumeBounds);
	VolumeBounds->SetBoxExtent(FVector(1000.0f, 1000.0f, 500.0f));
	VolumeBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// Trigger idiom: WorldDynamic so object-type queries for static geometry never see this box,
	// no blocking responses at all, and only pawns overlap it.
	VolumeBounds->SetCollisionObjectType(ECC_WorldDynamic);
	VolumeBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	VolumeBounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	VolumeBounds->SetGenerateOverlapEvents(true);
	VolumeBounds->SetLineThickness(2.0f);
	VolumeBounds->ShapeColor = FColor::Cyan;
}

bool AActorPhaseVolume::ResolvePhaseForViewer_Implementation(APlayerState* Viewer, FActorPhaseKey& OutKey) const
{
	if (Resolver.GetObject())
	{
		FActorPhaseKey Resolved;
		if (IActorPhaseResolverInterface::Execute_ResolvePhaseForViewer(Resolver.GetObject(), Viewer, const_cast<AActorPhaseVolume*>(this), Resolved) && Resolved.IsValid())
		{
			OutKey = Resolved;
			return true;
		}
	}
	if (bUseFixedPhase && FixedPhase.IsValid())
	{
		OutKey = FixedPhase;
		return true;
	}
	return false;
}

void AActorPhaseVolume::ReapplyForPawn(APawn* Pawn)
{
	if (!Pawn || !HasAuthority() || !Occupants.Contains(Pawn))
	{
		return;
	}
	if (APlayerState* PlayerState = Pawn->GetPlayerState())
	{
		ApplyPhaseToPawn(Pawn, PlayerState);
	}
}

void AActorPhaseVolume::ReapplyForPlayerState(APlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return;
	}
	ReapplyForPawn(PlayerState->GetPawn());
}

void AActorPhaseVolume::SetFixedPhase(const FActorPhaseKey& Key, const bool bEnable)
{
	bUseFixedPhase = bEnable && Key.IsValid();
	FixedPhase = bUseFixedPhase ? Key : FActorPhaseKey();
	if (HasAuthority())
	{
		ReapplyAll();
	}
}

void AActorPhaseVolume::SetResolver(TScriptInterface<IActorPhaseResolverInterface> InResolver)
{
	Resolver = InResolver;
	if (HasAuthority())
	{
		ReapplyAll();
	}
}

void AActorPhaseVolume::ReapplyAll()
{
	TArray<TWeakObjectPtr<APawn>> Snapshot = Occupants.Array();
	for (const TWeakObjectPtr<APawn>& PawnPtr : Snapshot)
	{
		ReapplyForPawn(PawnPtr.Get());
	}
}

FBox AActorPhaseVolume::GetWorldBounds() const
{
	return VolumeBounds ? VolumeBounds->Bounds.GetBox() : FBox(ForceInit);
}

bool AActorPhaseVolume::ContainsPoint(const FVector& Point) const
{
	if (!VolumeBounds)
	{
		return false;
	}
	const FVector Local = VolumeBounds->GetComponentTransform().InverseTransformPosition(Point);
	const FVector Extent = VolumeBounds->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

bool AActorPhaseVolume::IsPawnInside(const APawn* Pawn) const
{
	return Pawn && VolumeBounds && VolumeBounds->IsOverlappingActor(Pawn);
}

void AActorPhaseVolume::GetOccupants(TArray<APlayerState*>& OutOccupants) const
{
	OutOccupants.Reset();
	for (const TWeakObjectPtr<APawn>& PawnPtr : Occupants)
	{
		const APawn* Pawn = PawnPtr.Get();
		if (APlayerState* PlayerState = Pawn ? Pawn->GetPlayerState() : nullptr)
		{
			OutOccupants.Add(PlayerState);
		}
	}
}

void AActorPhaseVolume::BeginPlay()
{
	Super::BeginPlay();

	if (UActorPhaseSubsystem* Subsystem = UActorPhaseSubsystem::Get(this))
	{
		Subsystem->RegisterVolume(this);
	}

	if (!HasAuthority())
	{
		return;
	}

	VolumeBounds->OnComponentBeginOverlap.AddDynamic(this, &AActorPhaseVolume::HandleBeginOverlap);
	VolumeBounds->OnComponentEndOverlap.AddDynamic(this, &AActorPhaseVolume::HandleEndOverlap);

	// Pawns already inside when the volume begins play (streamed-in level, late spawn).
	TArray<AActor*> Overlapping;
	VolumeBounds->GetOverlappingActors(Overlapping, APawn::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		if (APawn* Pawn = Cast<APawn>(Actor))
		{
			TryApplyPhase(Pawn, PlayerStateRetryAttempts);
		}
	}
}

void AActorPhaseVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		TArray<TWeakObjectPtr<APawn>> Snapshot = Occupants.Array();
		for (const TWeakObjectPtr<APawn>& PawnPtr : Snapshot)
		{
			if (APawn* Pawn = PawnPtr.Get())
			{
				RemovePawn(Pawn);
			}
		}
		Occupants.Empty();
	}

	if (UActorPhaseSubsystem* Subsystem = UActorPhaseSubsystem::Get(this))
	{
		Subsystem->UnregisterVolume(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AActorPhaseVolume::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || Occupants.Contains(Pawn))
	{
		return;
	}
	TryApplyPhase(Pawn, PlayerStateRetryAttempts);
}

void AActorPhaseVolume::HandleEndOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn)
	{
		return;
	}
	// A pawn has several primitives; only leave when none of them overlaps the box any more.
	if (VolumeBounds->IsOverlappingActor(Pawn))
	{
		return;
	}
	RemovePawn(Pawn);
}

void AActorPhaseVolume::TryApplyPhase(TWeakObjectPtr<APawn> Pawn, const int32 AttemptsLeft)
{
	APawn* PawnPtr = Pawn.Get();
	if (!PawnPtr || !HasAuthority())
	{
		return;
	}

	APlayerState* PlayerState = PawnPtr->GetPlayerState();
	if (!PlayerState)
	{
		// Spawn-inside case: the overlap fires before possession. Retry until the PlayerState exists.
		if (AttemptsLeft > 0)
		{
			FTimerHandle RetryHandle;
			GetWorldTimerManager().SetTimer(RetryHandle, FTimerDelegate::CreateUObject(
				this, &AActorPhaseVolume::TryApplyPhase, Pawn, AttemptsLeft - 1), PlayerStateRetrySeconds, false);
		}
		return;
	}

	// The pawn may have left while we waited for possession.
	if (!VolumeBounds->IsOverlappingActor(PawnPtr))
	{
		return;
	}

	Occupants.Add(PawnPtr);
	ApplyPhaseToPawn(PawnPtr, PlayerState);
	OnPawnEntered.Broadcast(this, PlayerState, PawnPtr);
}

void AActorPhaseVolume::ApplyPhaseToPawn(APawn* Pawn, APlayerState* PlayerState)
{
	UActorPhaseMembershipComponent* Membership = PlayerState->FindComponentByClass<UActorPhaseMembershipComponent>();
	if (!Membership && GetDefault<UActorPhasingSettings>()->bAutoAddMembershipComponent)
	{
		Membership = NewObject<UActorPhaseMembershipComponent>(PlayerState, UActorPhaseMembershipComponent::StaticClass(), TEXT("ActorPhaseMembership"));
		Membership->RegisterComponent();
	}
	if (!Membership)
	{
		UE_LOG(LogActorPhasing, Warning, TEXT("%s: %s has no membership component and auto-add is disabled; phase not applied."),
			*GetName(), *PlayerState->GetPlayerName());
		return;
	}

	FActorPhaseKey Key;
	if (ResolvePhaseForViewer(PlayerState, Key) && Key.IsValid())
	{
		Membership->SetSinglePhase(Key);
	}
	else if (bResetToBaseOnExit)
	{
		// No phase for this viewer here: make sure a stale selection from elsewhere is not kept.
		Membership->ResetToBase();
	}

	if (bMaintainCollisionScope)
	{
		if (UActorPhaseSubsystem* Subsystem = UActorPhaseSubsystem::Get(this))
		{
			Subsystem->RegisterPawnInCollisionScope(this, Pawn);
		}
	}
}

void AActorPhaseVolume::RemovePawn(APawn* Pawn)
{
	if (!Occupants.Remove(Pawn))
	{
		return;
	}

	APlayerState* PlayerState = Pawn->GetPlayerState();

	if (UActorPhaseSubsystem* Subsystem = UActorPhaseSubsystem::Get(this))
	{
		Subsystem->UnregisterPawnFromCollisionScope(this, Pawn);
	}

	if (bResetToBaseOnExit && PlayerState)
	{
		if (UActorPhaseMembershipComponent* Membership = PlayerState->FindComponentByClass<UActorPhaseMembershipComponent>())
		{
			Membership->ResetToBase();
		}
	}

	OnPawnExited.Broadcast(this, PlayerState, Pawn);
}
