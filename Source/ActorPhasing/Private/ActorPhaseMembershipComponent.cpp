// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhaseMembershipComponent.h"

#include "ActorPhaseComponent.h"
#include "ActorPhaseSubsystem.h"
#include "ActorPhasingModule.h"
#include "ActorPhasingSettings.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

UActorPhaseMembershipComponent::UActorPhaseMembershipComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	Membership.Add(FActorPhaseKey::Base());
}

void UActorPhaseMembershipComponent::SetMembership(const TArray<FActorPhaseKey>& NewMembership)
{
	TArray<FActorPhaseKey> Sanitized = NewMembership;
	SanitizeMembership(Sanitized);
	ApplyMembership(MoveTemp(Sanitized));
}

void UActorPhaseMembershipComponent::SetSinglePhase(const FActorPhaseKey& Key)
{
	TArray<FActorPhaseKey> Keys;
	Keys.Add(Key);
	SanitizeMembership(Keys);
	ApplyMembership(MoveTemp(Keys));
}

void UActorPhaseMembershipComponent::ResetToBase()
{
	TArray<FActorPhaseKey> Keys;
	Keys.Add(FActorPhaseKey::Base());
	ApplyMembership(MoveTemp(Keys));
}

bool UActorPhaseMembershipComponent::HasPhase(const FActorPhaseKey& Key) const
{
	return Membership.Contains(Key);
}

bool UActorPhaseMembershipComponent::IsInBasePhase() const
{
	return Membership.Num() == 1 && Membership[0].IsBase();
}

void UActorPhaseMembershipComponent::SetObservedPhases(const TArray<FActorPhaseKey>& NewObservedPhases)
{
	TArray<FActorPhaseKey> Sanitized = NewObservedPhases;
	SanitizeMembership(Sanitized);
	ApplyObservedPhases(MoveTemp(Sanitized));
}

void UActorPhaseMembershipComponent::AddObservedPhase(const FActorPhaseKey& Key)
{
	if (!Key.IsValid() || ObservedPhases.Contains(Key))
	{
		return;
	}
	TArray<FActorPhaseKey> Keys = ObservedPhases;
	Keys.Add(Key);
	ApplyObservedPhases(MoveTemp(Keys));
}

void UActorPhaseMembershipComponent::RemoveObservedPhase(const FActorPhaseKey& Key)
{
	if (!ObservedPhases.Contains(Key))
	{
		return;
	}
	TArray<FActorPhaseKey> Keys = ObservedPhases;
	Keys.Remove(Key);
	ApplyObservedPhases(MoveTemp(Keys));
}

void UActorPhaseMembershipComponent::ClearObservedPhases()
{
	ApplyObservedPhases(TArray<FActorPhaseKey>());
}

bool UActorPhaseMembershipComponent::IntersectsKeys(const TArray<FActorPhaseKey>& Keys) const
{
	for (const FActorPhaseKey& Key : Keys)
	{
		if (Membership.Contains(Key))
		{
			return true;
		}
	}
	return false;
}

APlayerState* UActorPhaseMembershipComponent::GetOwningPlayerState() const
{
	AActor* Owner = GetOwner();
	if (APlayerState* PlayerState = Cast<APlayerState>(Owner))
	{
		return PlayerState;
	}
	if (const AController* Controller = Cast<AController>(Owner))
	{
		return Controller->PlayerState;
	}
	return nullptr;
}

APlayerController* UActorPhaseMembershipComponent::GetOwningPlayerController() const
{
	AActor* Owner = GetOwner();
	if (APlayerController* Controller = Cast<APlayerController>(Owner))
	{
		return Controller;
	}
	if (const APlayerState* PlayerState = Cast<APlayerState>(Owner))
	{
		return PlayerState->GetPlayerController();
	}
	return nullptr;
}

APawn* UActorPhaseMembershipComponent::GetViewedPawn() const
{
	AActor* Owner = GetOwner();
	if (const APlayerState* PlayerState = Cast<APlayerState>(Owner))
	{
		return PlayerState->GetPawn();
	}
	if (const AController* Controller = Cast<AController>(Owner))
	{
		return Controller->GetPawn();
	}
	return nullptr;
}

void UActorPhaseMembershipComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UActorPhaseMembershipComponent, Membership, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UActorPhaseMembershipComponent, ObservedPhases, COND_OwnerOnly);
}

void UActorPhaseMembershipComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsServerLike())
	{
		return;
	}

	if (UActorPhaseSubsystem* Subsystem = GetPhaseSubsystem())
	{
		Subsystem->RegisterMembership(this);
		bRegistered = true;
	}

	BindPawnEvents();

	if (APawn* Pawn = GetViewedPawn())
	{
		PushMembershipToPawn(Pawn);
	}
}

void UActorPhaseMembershipComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindPawnEvents();

	if (bRegistered)
	{
		bRegistered = false;
		if (UActorPhaseSubsystem* Subsystem = GetPhaseSubsystem())
		{
			Subsystem->UnregisterMembership(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UActorPhaseMembershipComponent::OnRep_Membership(const TArray<FActorPhaseKey>& OldMembership)
{
	OnMembershipChanged.Broadcast(this, OldMembership, Membership);
}

void UActorPhaseMembershipComponent::OnRep_ObservedPhases(const TArray<FActorPhaseKey>& OldObservedPhases)
{
	OnObservedPhasesChanged.Broadcast(this, OldObservedPhases, ObservedPhases);
}

void UActorPhaseMembershipComponent::HandlePlayerStatePawnSet(APlayerState* /*Player*/, APawn* NewPawn, APawn* /*OldPawn*/)
{
	// The previous pawn keeps the keys it had (a corpse stays in the phase it died in).
	if (NewPawn)
	{
		PushMembershipToPawn(NewPawn);
	}
	if (UActorPhaseSubsystem* Subsystem = bRegistered ? GetPhaseSubsystem() : nullptr)
	{
		Subsystem->RefreshViewerBindings(this);
	}
}

void UActorPhaseMembershipComponent::HandleControllerPawnChanged(APawn* /*OldPawn*/, APawn* NewPawn)
{
	if (NewPawn)
	{
		PushMembershipToPawn(NewPawn);
	}
	if (UActorPhaseSubsystem* Subsystem = bRegistered ? GetPhaseSubsystem() : nullptr)
	{
		Subsystem->RefreshViewerBindings(this);
	}
}

void UActorPhaseMembershipComponent::ApplyMembership(TArray<FActorPhaseKey> Sanitized)
{
	if (!IsServerLike())
	{
		UE_LOG(LogActorPhasing, Warning, TEXT("%s: membership can only be changed on the authority."), *GetPathName());
		return;
	}

	if (Sanitized.Num() == 0)
	{
		Sanitized.Add(FActorPhaseKey::Base());
	}

	if (Sanitized == Membership)
	{
		return;
	}

	const TArray<FActorPhaseKey> OldMembership = MoveTemp(Membership);
	Membership = MoveTemp(Sanitized);

	UE_LOG(LogActorPhasing, Verbose, TEXT("%s: membership changed to [%s]"), *GetPathName(),
		*FString::JoinBy(Membership, TEXT(", "), [](const FActorPhaseKey& Key) { return Key.ToString(); }));

	// Order matters: the pawn's keys change first so the subsystem's channel refresh for this
	// viewer (and for every other viewer looking at this pawn) evaluates the new state once.
	if (APawn* Pawn = GetViewedPawn())
	{
		PushMembershipToPawn(Pawn);
	}

	if (UActorPhaseSubsystem* Subsystem = bRegistered ? GetPhaseSubsystem() : nullptr)
	{
		Subsystem->NotifyMembershipChanged(this, OldMembership);
	}

	OnMembershipChanged.Broadcast(this, OldMembership, Membership);
}

void UActorPhaseMembershipComponent::ApplyObservedPhases(TArray<FActorPhaseKey> Sanitized)
{
	if (!IsServerLike())
	{
		UE_LOG(LogActorPhasing, Warning, TEXT("%s: observed phases can only be changed on the authority."), *GetPathName());
		return;
	}
	if (Sanitized == ObservedPhases)
	{
		return;
	}

	const TArray<FActorPhaseKey> OldObserved = MoveTemp(ObservedPhases);
	ObservedPhases = MoveTemp(Sanitized);

	UE_LOG(LogActorPhasing, Verbose, TEXT("%s: observed phases changed to [%s]"), *GetPathName(),
		*FString::JoinBy(ObservedPhases, TEXT(", "), [](const FActorPhaseKey& Key) { return Key.ToString(); }));

	// Observation never touches the pawn: only this viewer's view of world content changes.
	if (UActorPhaseSubsystem* Subsystem = bRegistered ? GetPhaseSubsystem() : nullptr)
	{
		Subsystem->NotifyObservedPhasesChanged(this, OldObserved);
	}

	OnObservedPhasesChanged.Broadcast(this, OldObserved, ObservedPhases);
}

void UActorPhaseMembershipComponent::PushMembershipToPawn(APawn* Pawn)
{
	if (!Pawn || !IsServerLike())
	{
		return;
	}

	UActorPhaseComponent* PhaseComponent = Pawn->FindComponentByClass<UActorPhaseComponent>();
	if (!PhaseComponent)
	{
		if (!GetDefault<UActorPhasingSettings>()->bAutoAddPawnPhaseComponent)
		{
			return;
		}
		PhaseComponent = NewObject<UActorPhaseComponent>(Pawn, UActorPhaseComponent::StaticClass(), TEXT("ActorPhase"));
		PhaseComponent->SetMirrorsOwnerMembership(true);
		PhaseComponent->RegisterComponent();
	}

	if (PhaseComponent->MirrorsOwnerMembership())
	{
		PhaseComponent->SetPhaseKeys(Membership);
	}
}

void UActorPhaseMembershipComponent::BindPawnEvents()
{
	if (bPawnEventsBound)
	{
		return;
	}
	AActor* Owner = GetOwner();
	if (APlayerState* PlayerState = Cast<APlayerState>(Owner))
	{
		PlayerState->OnPawnSet.AddDynamic(this, &UActorPhaseMembershipComponent::HandlePlayerStatePawnSet);
		bPawnEventsBound = true;
	}
	else if (AController* Controller = Cast<AController>(Owner))
	{
		Controller->OnPossessedPawnChanged.AddDynamic(this, &UActorPhaseMembershipComponent::HandleControllerPawnChanged);
		bPawnEventsBound = true;
	}
}

void UActorPhaseMembershipComponent::UnbindPawnEvents()
{
	if (!bPawnEventsBound)
	{
		return;
	}
	bPawnEventsBound = false;
	AActor* Owner = GetOwner();
	if (APlayerState* PlayerState = Cast<APlayerState>(Owner))
	{
		PlayerState->OnPawnSet.RemoveDynamic(this, &UActorPhaseMembershipComponent::HandlePlayerStatePawnSet);
	}
	else if (AController* Controller = Cast<AController>(Owner))
	{
		Controller->OnPossessedPawnChanged.RemoveDynamic(this, &UActorPhaseMembershipComponent::HandleControllerPawnChanged);
	}
}

UActorPhaseSubsystem* UActorPhaseMembershipComponent::GetPhaseSubsystem() const
{
	return UActorPhaseSubsystem::Get(this);
}

bool UActorPhaseMembershipComponent::IsServerLike() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UActorPhaseMembershipComponent::SanitizeMembership(TArray<FActorPhaseKey>& Keys)
{
	TArray<FActorPhaseKey> Clean;
	Clean.Reserve(Keys.Num());
	for (const FActorPhaseKey& Key : Keys)
	{
		if (Key.IsValid() && !Clean.Contains(Key))
		{
			Clean.Add(Key);
		}
	}
	Keys = MoveTemp(Clean);
}
