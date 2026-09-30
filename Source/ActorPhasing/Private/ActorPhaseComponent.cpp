// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhaseComponent.h"

#include "ActorPhaseSubsystem.h"
#include "ActorPhasingModule.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UActorPhaseComponent::UActorPhaseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UActorPhaseComponent::SetPhaseKeys(const TArray<FActorPhaseKey>& NewKeys)
{
	TArray<FActorPhaseKey> Sanitized = NewKeys;
	SanitizeKeys(Sanitized);
	ApplyKeys(MoveTemp(Sanitized));
}

void UActorPhaseComponent::SetSinglePhase(const FActorPhaseKey& Key)
{
	TArray<FActorPhaseKey> Keys;
	if (Key.IsValid())
	{
		Keys.Add(Key);
	}
	ApplyKeys(MoveTemp(Keys));
}

void UActorPhaseComponent::AddPhaseKey(const FActorPhaseKey& Key)
{
	if (!Key.IsValid() || PhaseKeys.Contains(Key))
	{
		return;
	}
	TArray<FActorPhaseKey> Keys = PhaseKeys;
	Keys.Add(Key);
	ApplyKeys(MoveTemp(Keys));
}

void UActorPhaseComponent::RemovePhaseKey(const FActorPhaseKey& Key)
{
	if (!PhaseKeys.Contains(Key))
	{
		return;
	}
	TArray<FActorPhaseKey> Keys = PhaseKeys;
	Keys.Remove(Key);
	ApplyKeys(MoveTemp(Keys));
}

void UActorPhaseComponent::ClearPhaseKeys()
{
	ApplyKeys(TArray<FActorPhaseKey>());
}

void UActorPhaseComponent::SetMirrorsOwnerMembership(const bool bInMirrors)
{
	if (bMirrorsOwnerMembership == bInMirrors)
	{
		return;
	}
	bMirrorsOwnerMembership = bInMirrors;
	NotifyObservabilityChanged();
}

void UActorPhaseComponent::SetVisibleToObservers(const bool bInVisible)
{
	if (bVisibleToObservers == bInVisible)
	{
		return;
	}
	bVisibleToObservers = bInVisible;
	NotifyObservabilityChanged();
}

void UActorPhaseComponent::NotifyObservabilityChanged()
{
	if (!bRegistered || !IsPhased())
	{
		return;
	}
	if (UActorPhaseSubsystem* Subsystem = GetPhaseSubsystem())
	{
		Subsystem->NotifyPhaseKeysChanged(this, PhaseKeys);
	}
}

bool UActorPhaseComponent::HasPhaseKey(const FActorPhaseKey& Key) const
{
	return PhaseKeys.Contains(Key);
}

bool UActorPhaseComponent::IsPhased() const
{
	for (const FActorPhaseKey& Key : PhaseKeys)
	{
		if (Key.IsValid())
		{
			return true;
		}
	}
	return false;
}

void UActorPhaseComponent::SetCollisionScope(UObject* Scope)
{
	UActorPhaseSubsystem* Subsystem = GetPhaseSubsystem();
	AActor* Owner = GetOwner();

	if (UObject* OldScope = CollisionScope.Get())
	{
		if (Subsystem && Owner)
		{
			Subsystem->UnregisterActorFromCollisionScope(OldScope, Owner);
		}
	}

	CollisionScope = Scope;

	if (Scope && Subsystem && Owner)
	{
		Subsystem->RegisterActorInCollisionScope(Scope, Owner);
	}
}

void UActorPhaseComponent::OnRegister()
{
	Super::OnRegister();
	SanitizeKeys(PhaseKeys);
	TryRegister();
}

void UActorPhaseComponent::BeginPlay()
{
	Super::BeginPlay();
	// The subsystem may not have existed yet when a level-placed component registered.
	TryRegister();
}

void UActorPhaseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TryUnregister();
	Super::EndPlay(EndPlayReason);
}

void UActorPhaseComponent::OnUnregister()
{
	TryUnregister();
	Super::OnUnregister();
}

void UActorPhaseComponent::ApplyKeys(TArray<FActorPhaseKey> Sanitized)
{
	if (Sanitized == PhaseKeys)
	{
		return;
	}

	const TArray<FActorPhaseKey> OldKeys = MoveTemp(PhaseKeys);
	PhaseKeys = MoveTemp(Sanitized);

	if (UActorPhaseSubsystem* Subsystem = bRegistered ? GetPhaseSubsystem() : nullptr)
	{
		Subsystem->NotifyPhaseKeysChanged(this, OldKeys);
	}

	OnPhaseKeysChanged.Broadcast(this, OldKeys, PhaseKeys);
}

void UActorPhaseComponent::TryRegister()
{
	if (bRegistered)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	if (UActorPhaseSubsystem* Subsystem = GetPhaseSubsystem())
	{
		Subsystem->RegisterPhaseComponent(this);
		bRegistered = true;

		if (UObject* Scope = CollisionScope.Get())
		{
			if (AActor* Owner = GetOwner())
			{
				Subsystem->RegisterActorInCollisionScope(Scope, Owner);
			}
		}
	}
}

void UActorPhaseComponent::TryUnregister()
{
	if (!bRegistered)
	{
		return;
	}
	bRegistered = false;
	if (UActorPhaseSubsystem* Subsystem = GetPhaseSubsystem())
	{
		if (UObject* Scope = CollisionScope.Get())
		{
			if (AActor* Owner = GetOwner())
			{
				Subsystem->UnregisterActorFromCollisionScope(Scope, Owner);
			}
		}
		Subsystem->UnregisterPhaseComponent(this);
	}
}

UActorPhaseSubsystem* UActorPhaseComponent::GetPhaseSubsystem() const
{
	return UActorPhaseSubsystem::Get(this);
}

void UActorPhaseComponent::SanitizeKeys(TArray<FActorPhaseKey>& Keys)
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
