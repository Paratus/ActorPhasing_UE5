// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhaseSubsystem.h"

#include "ActorPhaseComponent.h"
#include "ActorPhaseMembershipComponent.h"
#include "ActorPhaseVolume.h"
#include "ActorPhasingDebug.h"
#include "ActorPhasingLibrary.h"
#include "ActorPhasingModule.h"
#include "ActorPhasingSettings.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/ActorChannel.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace
{
	FString JoinKeys(const TArray<FActorPhaseKey>& Keys)
	{
		return FString::JoinBy(Keys, TEXT(", "), [](const FActorPhaseKey& Key) { return Key.ToString(); });
	}
}

UActorPhaseSubsystem* UActorPhaseSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UActorPhaseSubsystem>() : nullptr;
}

bool UActorPhaseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UActorPhaseSubsystem::Deinitialize()
{
	PhaseComponentsByActor.Empty();
	ActorsByKey.Empty();
	Memberships.Empty();
	PinnedViewersByMembership.Empty();
	ViewerCountByKey.Empty();
	ViewerCache.Empty();
	Scopes.Empty();
	IdleRules.Empty();
	Volumes.Empty();
	Super::Deinitialize();
}

void UActorPhaseSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);

	PollIdleRules(DeltaTime);

	if (ActorPhasingDebug::IsDrawEnabled())
	{
		DrawDebug();
	}
}

TStatId UActorPhaseSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UActorPhaseSubsystem, STATGROUP_Tickables);
}

bool UActorPhaseSubsystem::IsTickable() const
{
	return Super::IsTickable() && IsServerLike() && (IdleRules.Num() > 0 || ActorPhasingDebug::IsDrawEnabled());
}

ETickableTickType UActorPhaseSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

// ---------------------------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------------------------

void UActorPhaseSubsystem::RegisterPhaseComponent(UActorPhaseComponent* Component)
{
	AActor* Owner = Component ? Component->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}
	PhaseComponentsByActor.Add(Owner, Component);
	for (const FActorPhaseKey& Key : Component->GetPhaseKeysRef())
	{
		ActorsByKey.FindOrAdd(Key).Add(Owner);
	}
	if (Component->GetPhaseKeysRef().Num() > 0)
	{
		OnActorPhaseKeysChangedNative.Broadcast(Owner, TArray<FActorPhaseKey>(), Component->GetPhaseKeysRef());
	}
}

void UActorPhaseSubsystem::UnregisterPhaseComponent(UActorPhaseComponent* Component)
{
	AActor* Owner = Component ? Component->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}
	PhaseComponentsByActor.Remove(Owner);
	for (const FActorPhaseKey& Key : Component->GetPhaseKeysRef())
	{
		if (TSet<TWeakObjectPtr<AActor>>* Actors = ActorsByKey.Find(Key))
		{
			Actors->Remove(Owner);
			if (Actors->Num() == 0)
			{
				ActorsByKey.Remove(Key);
			}
		}
	}
	if (Component->GetPhaseKeysRef().Num() > 0)
	{
		OnActorPhaseKeysChangedNative.Broadcast(Owner, Component->GetPhaseKeysRef(), TArray<FActorPhaseKey>());
	}
}

void UActorPhaseSubsystem::NotifyPhaseKeysChanged(UActorPhaseComponent* Component, const TArray<FActorPhaseKey>& OldKeys)
{
	AActor* Owner = Component ? Component->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}

	for (const FActorPhaseKey& Key : OldKeys)
	{
		if (TSet<TWeakObjectPtr<AActor>>* Actors = ActorsByKey.Find(Key))
		{
			Actors->Remove(Owner);
			if (Actors->Num() == 0)
			{
				ActorsByKey.Remove(Key);
			}
		}
	}
	for (const FActorPhaseKey& Key : Component->GetPhaseKeysRef())
	{
		ActorsByKey.FindOrAdd(Key).Add(Owner);
	}

	if (ActorPhasingDebug::IsVerboseLogEnabled())
	{
		UE_LOG(LogActorPhasing, Log, TEXT("%s: phase keys [%s] -> [%s]"), *Owner->GetName(), *JoinKeys(OldKeys), *JoinKeys(Component->GetPhaseKeysRef()));
	}

	OnActorPhaseKeysChangedNative.Broadcast(Owner, OldKeys, Component->GetPhaseKeysRef());

	if (!IsServerLike())
	{
		return;
	}

	// A dormant actor is never re-evaluated for relevancy; wake it so the driver reconsiders it.
	Owner->FlushNetDormancy();
	RefreshActorChannels(Owner);

	if (UObject* Scope = Component->GetCollisionScope())
	{
		RecomputeCollisionScope(Scope);
	}
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		RecomputeScopesContaining(Pawn);
	}
}

void UActorPhaseSubsystem::RegisterMembership(UActorPhaseMembershipComponent* Component)
{
	if (!Component || Memberships.Contains(Component))
	{
		return;
	}
	Memberships.Add(Component);
	AddViewerKeys(Component->GetMembershipRef(), +1);
	AddObserverKeys(Component->GetObservedPhasesRef(), +1);
	RefreshViewerBindings(Component);
	OnMembershipRegisteredNative.Broadcast(Component);
}

void UActorPhaseSubsystem::UnregisterMembership(UActorPhaseMembershipComponent* Component)
{
	if (!Component || !Memberships.Contains(Component))
	{
		return;
	}
	Memberships.Remove(Component);
	AddViewerKeys(Component->GetMembershipRef(), -1);
	AddObserverKeys(Component->GetObservedPhasesRef(), -1);

	if (TArray<TObjectKey<AActor>>* Pinned = PinnedViewersByMembership.Find(Component))
	{
		for (const TObjectKey<AActor>& ViewerKey : *Pinned)
		{
			ViewerCache.Remove(ViewerKey);
		}
		PinnedViewersByMembership.Remove(Component);
	}
	OnMembershipUnregisteredNative.Broadcast(Component);
}

void UActorPhaseSubsystem::NotifyMembershipChanged(UActorPhaseMembershipComponent* Component, const TArray<FActorPhaseKey>& OldMembership)
{
	if (!Component || !Memberships.Contains(Component))
	{
		return;
	}
	AddViewerKeys(OldMembership, -1);
	AddViewerKeys(Component->GetMembershipRef(), +1);

	if (ActorPhasingDebug::IsVerboseLogEnabled())
	{
		const APlayerState* PlayerState = Component->GetOwningPlayerState();
		UE_LOG(LogActorPhasing, Log, TEXT("%s: membership [%s] -> [%s]"),
			PlayerState ? *PlayerState->GetPlayerName() : *Component->GetPathName(),
			*JoinKeys(OldMembership), *JoinKeys(Component->GetMembershipRef()));
	}

	OnMembershipChangedNative.Broadcast(Component, OldMembership, Component->GetMembershipRef());

	if (!IsServerLike())
	{
		return;
	}

	// A viewer that is not in a phase anymore must lose that phase's actors right away.
	if (APlayerController* Controller = Component->GetOwningPlayerController())
	{
		RefreshViewerChannels(Controller);
	}
}

void UActorPhaseSubsystem::NotifyObservedPhasesChanged(UActorPhaseMembershipComponent* Component, const TArray<FActorPhaseKey>& OldObservedPhases)
{
	if (!Component || !Memberships.Contains(Component))
	{
		return;
	}
	AddObserverKeys(OldObservedPhases, -1);
	AddObserverKeys(Component->GetObservedPhasesRef(), +1);

	if (ActorPhasingDebug::IsVerboseLogEnabled())
	{
		const APlayerState* PlayerState = Component->GetOwningPlayerState();
		UE_LOG(LogActorPhasing, Log, TEXT("%s: observing [%s] -> [%s]"),
			PlayerState ? *PlayerState->GetPlayerName() : *Component->GetPathName(),
			*JoinKeys(OldObservedPhases), *JoinKeys(Component->GetObservedPhasesRef()));
	}

	OnObservedPhasesChangedNative.Broadcast(Component, OldObservedPhases, Component->GetObservedPhasesRef());

	if (!IsServerLike())
	{
		return;
	}

	// Content of a phase the viewer stopped observing disappears right away.
	if (APlayerController* Controller = Component->GetOwningPlayerController())
	{
		RefreshViewerChannels(Controller);
	}
}

void UActorPhaseSubsystem::RefreshViewerBindings(UActorPhaseMembershipComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (TArray<TObjectKey<AActor>>* Pinned = PinnedViewersByMembership.Find(Component))
	{
		for (const TObjectKey<AActor>& ViewerKey : *Pinned)
		{
			ViewerCache.Remove(ViewerKey);
		}
		Pinned->Reset();
	}

	PinViewer(Component->GetOwner(), Component);
	PinViewer(Component->GetOwningPlayerState(), Component);
	PinViewer(Component->GetOwningPlayerController(), Component);
	PinViewer(Component->GetViewedPawn(), Component);
	OnViewerBindingsRefreshedNative.Broadcast(Component);
}

void UActorPhaseSubsystem::RegisterVolume(AActorPhaseVolume* Volume)
{
	if (Volume)
	{
		Volumes.AddUnique(Volume);
	}
}

void UActorPhaseSubsystem::UnregisterVolume(AActorPhaseVolume* Volume)
{
	Volumes.Remove(Volume);
}

// ---------------------------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------------------------

UActorPhaseComponent* UActorPhaseSubsystem::FindPhaseComponent(const AActor* Actor) const
{
	if (!Actor)
	{
		return nullptr;
	}
	const TWeakObjectPtr<UActorPhaseComponent>* Found = PhaseComponentsByActor.Find(Actor);
	return Found ? Found->Get() : nullptr;
}

UActorPhaseMembershipComponent* UActorPhaseSubsystem::FindMembership(const AActor* Viewer) const
{
	if (!Viewer)
	{
		return nullptr;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (FViewerCacheEntry* Entry = ViewerCache.Find(Viewer))
	{
		if (Entry->bPinned || Entry->Membership.IsValid() || Now < Entry->NextRecheckTime)
		{
			return Entry->Membership.Get();
		}
	}

	// Slow path: resolve through the library's viewer rules and cache the answer. Viewers that
	// have no component are re-checked periodically in case one is added later.
	UActorPhaseMembershipComponent* Resolved = nullptr;
	if (const APlayerController* Controller = Cast<APlayerController>(Viewer))
	{
		Resolved = Controller->FindComponentByClass<UActorPhaseMembershipComponent>();
		if (!Resolved && Controller->PlayerState)
		{
			Resolved = Controller->PlayerState->FindComponentByClass<UActorPhaseMembershipComponent>();
		}
	}
	else if (const APlayerState* PlayerState = Cast<APlayerState>(Viewer))
	{
		Resolved = PlayerState->FindComponentByClass<UActorPhaseMembershipComponent>();
		if (!Resolved && PlayerState->GetPlayerController())
		{
			Resolved = PlayerState->GetPlayerController()->FindComponentByClass<UActorPhaseMembershipComponent>();
		}
	}
	else if (const APawn* Pawn = Cast<APawn>(Viewer))
	{
		if (const APlayerState* PawnState = Pawn->GetPlayerState())
		{
			Resolved = PawnState->FindComponentByClass<UActorPhaseMembershipComponent>();
		}
		if (!Resolved && Pawn->GetController())
		{
			Resolved = Pawn->GetController()->FindComponentByClass<UActorPhaseMembershipComponent>();
		}
	}

	FViewerCacheEntry& Entry = ViewerCache.FindOrAdd(Viewer);
	Entry.Membership = Resolved;
	Entry.bPinned = false;
	Entry.NextRecheckTime = Now + GetDefault<UActorPhasingSettings>()->ViewerLookupRecheckSeconds;
	return Resolved;
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

int32 UActorPhaseSubsystem::GetViewerCount(const FActorPhaseKey& Key) const
{
	return ViewerCountByKey.FindRef(Key);
}

int32 UActorPhaseSubsystem::GetObserverCount(const FActorPhaseKey& Key) const
{
	return ObserverCountByKey.FindRef(Key);
}

int32 UActorPhaseSubsystem::GetActorCount(const FActorPhaseKey& Key) const
{
	const TSet<TWeakObjectPtr<AActor>>* Actors = ActorsByKey.Find(Key);
	return Actors ? Actors->Num() : 0;
}

void UActorPhaseSubsystem::GetActorsInPhase(const FActorPhaseKey& Key, TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	if (const TSet<TWeakObjectPtr<AActor>>* Actors = ActorsByKey.Find(Key))
	{
		for (const TWeakObjectPtr<AActor>& Actor : *Actors)
		{
			if (AActor* Resolved = Actor.Get())
			{
				OutActors.Add(Resolved);
			}
		}
	}
}

void UActorPhaseSubsystem::GetViewersInPhase(const FActorPhaseKey& Key, TArray<APlayerState*>& OutViewers) const
{
	OutViewers.Reset();
	for (const TWeakObjectPtr<UActorPhaseMembershipComponent>& Membership : Memberships)
	{
		const UActorPhaseMembershipComponent* Component = Membership.Get();
		if (Component && Component->HasPhase(Key))
		{
			if (APlayerState* PlayerState = Component->GetOwningPlayerState())
			{
				OutViewers.Add(PlayerState);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Collision scopes
// ---------------------------------------------------------------------------------------------

void UActorPhaseSubsystem::ForEachPhasedActor(TFunctionRef<void(AActor*, const TArray<FActorPhaseKey>&)> Visitor) const
{
	for (const TPair<TObjectKey<AActor>, TWeakObjectPtr<UActorPhaseComponent>>& Pair : PhaseComponentsByActor)
	{
		AActor* Actor = Pair.Key.ResolveObjectPtr();
		const UActorPhaseComponent* Component = Pair.Value.Get();
		if (Actor && Component)
		{
			Visitor(Actor, Component->GetPhaseKeysRef());
		}
	}
}

void UActorPhaseSubsystem::ForEachMembership(TFunctionRef<void(UActorPhaseMembershipComponent*)> Visitor) const
{
	for (const TWeakObjectPtr<UActorPhaseMembershipComponent>& Weak : Memberships)
	{
		if (UActorPhaseMembershipComponent* Membership = Weak.Get())
		{
			Visitor(Membership);
		}
	}
}

void UActorPhaseSubsystem::RegisterPawnInCollisionScope(UObject* Scope, APawn* Pawn)
{
	if (!Scope || !Pawn)
	{
		return;
	}
	FCollisionScope& Entry = Scopes.FindOrAdd(Scope);
	Entry.Pawns.AddUnique(Pawn);
	RecomputeCollisionScope(Scope);
}

void UActorPhaseSubsystem::UnregisterPawnFromCollisionScope(UObject* Scope, APawn* Pawn)
{
	FCollisionScope* Entry = Scope ? Scopes.Find(Scope) : nullptr;
	if (!Entry || !Pawn)
	{
		return;
	}
	Entry->Pawns.Remove(Pawn);
	ClearPawnIgnores(Pawn, *Entry);
	if (Entry->Pawns.Num() == 0 && Entry->Actors.Num() == 0)
	{
		Scopes.Remove(Scope);
	}
}

void UActorPhaseSubsystem::RegisterActorInCollisionScope(UObject* Scope, AActor* Actor)
{
	if (!Scope || !Actor)
	{
		return;
	}
	FCollisionScope& Entry = Scopes.FindOrAdd(Scope);
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		Entry.Pawns.AddUnique(Pawn);
	}
	else
	{
		Entry.Actors.AddUnique(Actor);
	}
	RecomputeCollisionScope(Scope);
}

void UActorPhaseSubsystem::UnregisterActorFromCollisionScope(UObject* Scope, AActor* Actor)
{
	FCollisionScope* Entry = Scope ? Scopes.Find(Scope) : nullptr;
	if (!Entry || !Actor)
	{
		return;
	}
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		UnregisterPawnFromCollisionScope(Scope, Pawn);
		return;
	}
	Entry->Actors.Remove(Actor);
	for (const TWeakObjectPtr<APawn>& PawnPtr : Entry->Pawns)
	{
		APawn* Pawn = PawnPtr.Get();
		UPrimitiveComponent* Root = Pawn ? Cast<UPrimitiveComponent>(Pawn->GetRootComponent()) : nullptr;
		if (Root)
		{
			Root->IgnoreActorWhenMoving(Actor, false);
		}
	}
	if (Entry->Pawns.Num() == 0 && Entry->Actors.Num() == 0)
	{
		Scopes.Remove(Scope);
	}
}

void UActorPhaseSubsystem::RecomputeCollisionScope(UObject* Scope)
{
	FCollisionScope* Entry = Scope ? Scopes.Find(Scope) : nullptr;
	if (!Entry || !GetDefault<UActorPhasingSettings>()->bMaintainMoveIgnore)
	{
		return;
	}

	Entry->Pawns.RemoveAll([](const TWeakObjectPtr<APawn>& Pawn) { return !Pawn.IsValid(); });
	Entry->Actors.RemoveAll([](const TWeakObjectPtr<AActor>& Actor) { return !Actor.IsValid(); });

	for (const TWeakObjectPtr<APawn>& PawnPtr : Entry->Pawns)
	{
		APawn* Pawn = PawnPtr.Get();
		UPrimitiveComponent* Root = Pawn ? Cast<UPrimitiveComponent>(Pawn->GetRootComponent()) : nullptr;
		if (!Root)
		{
			continue;
		}

		for (const TWeakObjectPtr<APawn>& OtherPtr : Entry->Pawns)
		{
			APawn* Other = OtherPtr.Get();
			if (!Other || Other == Pawn)
			{
				continue;
			}
			Root->IgnoreActorWhenMoving(Other, !UActorPhasingLibrary::AreActorsInCompatiblePhases(Pawn, Other));
		}

		for (const TWeakObjectPtr<AActor>& ActorPtr : Entry->Actors)
		{
			AActor* Actor = ActorPtr.Get();
			if (!Actor)
			{
				continue;
			}
			Root->IgnoreActorWhenMoving(Actor, !UActorPhasingLibrary::AreActorsInCompatiblePhases(Pawn, Actor));
		}
	}
}

void UActorPhaseSubsystem::RecomputeScopesContaining(const APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	TArray<UObject*> Affected;
	for (TPair<TObjectKey<UObject>, FCollisionScope>& Pair : Scopes)
	{
		if (Pair.Value.Pawns.Contains(Pawn))
		{
			if (UObject* Scope = Pair.Key.ResolveObjectPtr())
			{
				Affected.Add(Scope);
			}
		}
	}
	for (UObject* Scope : Affected)
	{
		RecomputeCollisionScope(Scope);
	}
}

void UActorPhaseSubsystem::ClearPawnIgnores(APawn* Pawn, const FCollisionScope& Scope) const
{
	UPrimitiveComponent* Root = Pawn ? Cast<UPrimitiveComponent>(Pawn->GetRootComponent()) : nullptr;
	for (const TWeakObjectPtr<APawn>& OtherPtr : Scope.Pawns)
	{
		APawn* Other = OtherPtr.Get();
		if (!Other || Other == Pawn)
		{
			continue;
		}
		if (Root)
		{
			Root->IgnoreActorWhenMoving(Other, false);
		}
		if (UPrimitiveComponent* OtherRoot = Cast<UPrimitiveComponent>(Other->GetRootComponent()))
		{
			OtherRoot->IgnoreActorWhenMoving(Pawn, false);
		}
	}
	if (Root)
	{
		for (const TWeakObjectPtr<AActor>& ActorPtr : Scope.Actors)
		{
			if (AActor* Actor = ActorPtr.Get())
			{
				Root->IgnoreActorWhenMoving(Actor, false);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Channels
// ---------------------------------------------------------------------------------------------

void UActorPhaseSubsystem::RefreshActorChannels(AActor* Actor)
{
	if (!Actor || !IsServerLike() || !GetDefault<UActorPhasingSettings>()->bCloseStaleChannelsImmediately)
	{
		return;
	}
	const UWorld* World = GetWorld();
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	if (!Driver)
	{
		return;
	}

	TArray<UActorChannel*> ToClose;
	for (UNetConnection* Connection : Driver->ClientConnections)
	{
		if (!Connection || !Connection->PlayerController)
		{
			continue;
		}
		UActorChannel* Channel = Connection->FindActorChannelRef(Actor);
		if (Channel && !Channel->Closing && !UActorPhasingLibrary::IsActorRelevantToViewer(Actor, Connection->PlayerController))
		{
			ToClose.Add(Channel);
		}
	}
	for (UActorChannel* Channel : ToClose)
	{
		Channel->Close(EChannelCloseReason::Relevancy);
	}
}

void UActorPhaseSubsystem::RefreshViewerChannels(AActor* Viewer)
{
	if (!Viewer || !IsServerLike() || !GetDefault<UActorPhasingSettings>()->bCloseStaleChannelsImmediately)
	{
		return;
	}
	APlayerController* Controller = ResolveViewerController(Viewer);
	UNetConnection* Connection = FindConnectionFor(Controller);
	if (!Connection)
	{
		return;
	}

	// Only actors that carry a phase component are closed here: a class that does not consult the
	// rule from IsNetRelevantFor would simply reopen its channel on the next update.
	TArray<UActorChannel*> ToClose;
	for (auto It = Connection->ActorChannelConstIterator(); It; ++It)
	{
		AActor* Actor = It.Key().Get();
		UActorChannel* Channel = It.Value();
		if (!Actor || !Channel || Channel->Closing || !PhaseComponentsByActor.Contains(Actor))
		{
			continue;
		}
		if (!UActorPhasingLibrary::IsActorRelevantToViewer(Actor, Controller))
		{
			ToClose.Add(Channel);
		}
	}
	for (UActorChannel* Channel : ToClose)
	{
		Channel->Close(EChannelCloseReason::Relevancy);
	}
}

// ---------------------------------------------------------------------------------------------
// Idle rules
// ---------------------------------------------------------------------------------------------

void UActorPhaseSubsystem::RegisterIdleRule(const FActorPhaseKey& Key, const float GraceSeconds)
{
	if (!Key.IsValid())
	{
		return;
	}
	FIdleRule& Rule = IdleRules.FindOrAdd(Key);
	Rule.GraceSeconds = GraceSeconds < 0.0f ? GetDefault<UActorPhasingSettings>()->DefaultIdleGraceSeconds : GraceSeconds;
	Rule.UnviewedFor = -1.0f;
}

void UActorPhaseSubsystem::ClearIdleRule(const FActorPhaseKey& Key)
{
	IdleRules.Remove(Key);
}

bool UActorPhaseSubsystem::HasIdleRule(const FActorPhaseKey& Key) const
{
	return IdleRules.Contains(Key);
}

void UActorPhaseSubsystem::PollIdleRules(const float DeltaTime)
{
	if (IdleRules.Num() == 0)
	{
		return;
	}

	TArray<FActorPhaseKey> Fired;
	for (TPair<FActorPhaseKey, FIdleRule>& Pair : IdleRules)
	{
		FIdleRule& Rule = Pair.Value;
		if (ViewerCountByKey.FindRef(Pair.Key) > 0 || ObserverCountByKey.FindRef(Pair.Key) > 0)
		{
			Rule.UnviewedFor = -1.0f;
			continue;
		}
		Rule.UnviewedFor = FMath::Max(0.0f, Rule.UnviewedFor) + DeltaTime;
		if (Rule.UnviewedFor >= Rule.GraceSeconds)
		{
			Fired.Add(Pair.Key);
		}
	}

	for (const FActorPhaseKey& Key : Fired)
	{
		IdleRules.Remove(Key);
		UE_LOG(LogActorPhasing, Verbose, TEXT("Phase %s has had no viewer for its grace period."), *Key.ToString());
		OnPhaseUnviewed.Broadcast(Key);
	}
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

bool UActorPhaseSubsystem::IsServerLike() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UActorPhaseSubsystem::AddViewerKeys(const TArray<FActorPhaseKey>& Keys, const int32 Delta)
{
	for (const FActorPhaseKey& Key : Keys)
	{
		int32& Count = ViewerCountByKey.FindOrAdd(Key);
		Count += Delta;
		if (Count <= 0)
		{
			ViewerCountByKey.Remove(Key);
		}
	}
}

void UActorPhaseSubsystem::AddObserverKeys(const TArray<FActorPhaseKey>& Keys, const int32 Delta)
{
	for (const FActorPhaseKey& Key : Keys)
	{
		int32& Count = ObserverCountByKey.FindOrAdd(Key);
		Count += Delta;
		if (Count <= 0)
		{
			ObserverCountByKey.Remove(Key);
		}
	}
}

void UActorPhaseSubsystem::PinViewer(const AActor* Viewer, UActorPhaseMembershipComponent* Component)
{
	if (!Viewer || !Component)
	{
		return;
	}
	FViewerCacheEntry& Entry = ViewerCache.FindOrAdd(Viewer);
	Entry.Membership = Component;
	Entry.bPinned = true;
	PinnedViewersByMembership.FindOrAdd(Component).AddUnique(Viewer);
}

void UActorPhaseSubsystem::UnpinViewer(const AActor* Viewer)
{
	if (Viewer)
	{
		ViewerCache.Remove(Viewer);
	}
}

APlayerController* UActorPhaseSubsystem::ResolveViewerController(const AActor* Viewer) const
{
	if (!Viewer)
	{
		return nullptr;
	}
	if (const APlayerController* Controller = Cast<APlayerController>(Viewer))
	{
		return const_cast<APlayerController*>(Controller);
	}
	if (const APlayerState* PlayerState = Cast<APlayerState>(Viewer))
	{
		return PlayerState->GetPlayerController();
	}
	if (const APawn* Pawn = Cast<APawn>(Viewer))
	{
		return Cast<APlayerController>(Pawn->GetController());
	}
	return nullptr;
}

UNetConnection* UActorPhaseSubsystem::FindConnectionFor(const APlayerController* Controller) const
{
	if (!Controller)
	{
		return nullptr;
	}
	if (UNetConnection* Direct = Controller->GetNetConnection())
	{
		return Direct;
	}
	const UWorld* World = GetWorld();
	const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	if (!Driver)
	{
		return nullptr;
	}
	for (UNetConnection* Connection : Driver->ClientConnections)
	{
		if (Connection && Connection->PlayerController == Controller)
		{
			return Connection;
		}
	}
	return nullptr;
}

FString UActorPhaseSubsystem::DescribeState() const
{
	TStringBuilder<2048> Out;
	Out.Appendf(TEXT("Actor Phasing (%s)\n"), IsServerLike() ? TEXT("authority") : TEXT("client"));

	Out.Appendf(TEXT("Viewers (%d):\n"), Memberships.Num());
	for (const TWeakObjectPtr<UActorPhaseMembershipComponent>& MembershipPtr : Memberships)
	{
		const UActorPhaseMembershipComponent* Membership = MembershipPtr.Get();
		if (!Membership)
		{
			continue;
		}
		const APlayerState* PlayerState = Membership->GetOwningPlayerState();
		if (Membership->GetObservedPhasesRef().Num() > 0)
		{
			Out.Appendf(TEXT("  %s -> [%s], observing [%s]\n"), PlayerState ? *PlayerState->GetPlayerName() : *Membership->GetPathName(),
				*JoinKeys(Membership->GetMembershipRef()), *JoinKeys(Membership->GetObservedPhasesRef()));
			continue;
		}
		Out.Appendf(TEXT("  %s -> [%s]\n"), PlayerState ? *PlayerState->GetPlayerName() : *Membership->GetPathName(), *JoinKeys(Membership->GetMembershipRef()));
	}

	Out.Appendf(TEXT("Phases (%d):\n"), ActorsByKey.Num());
	for (const TPair<FActorPhaseKey, TSet<TWeakObjectPtr<AActor>>>& Pair : ActorsByKey)
	{
		Out.Appendf(TEXT("  %s: %d actor(s), %d viewer(s), %d observer(s)\n"), *Pair.Key.ToString(), Pair.Value.Num(), ViewerCountByKey.FindRef(Pair.Key), ObserverCountByKey.FindRef(Pair.Key));
	}

	Out.Appendf(TEXT("Collision scopes (%d):\n"), Scopes.Num());
	for (const TPair<TObjectKey<UObject>, FCollisionScope>& Pair : Scopes)
	{
		const UObject* Scope = Pair.Key.ResolveObjectPtr();
		Out.Appendf(TEXT("  %s: %d pawn(s), %d actor(s)\n"), Scope ? *Scope->GetName() : TEXT("<stale>"), Pair.Value.Pawns.Num(), Pair.Value.Actors.Num());
	}

	Out.Appendf(TEXT("Idle rules (%d):\n"), IdleRules.Num());
	for (const TPair<FActorPhaseKey, FIdleRule>& Pair : IdleRules)
	{
		Out.Appendf(TEXT("  %s: grace %.1fs, unviewed for %.1fs\n"), *Pair.Key.ToString(), Pair.Value.GraceSeconds, FMath::Max(0.0f, Pair.Value.UnviewedFor));
	}

	return Out.ToString();
}

void UActorPhaseSubsystem::DrawDebug() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (const TWeakObjectPtr<AActorPhaseVolume>& VolumePtr : Volumes)
	{
		const AActorPhaseVolume* Volume = VolumePtr.Get();
		const UBoxComponent* Box = Volume ? Volume->GetVolumeBounds() : nullptr;
		if (Box)
		{
			DrawDebugBox(World, Box->GetComponentLocation(), Box->GetScaledBoxExtent(), Box->GetComponentQuat(), FColor::Cyan, false, -1.0f, 0, 2.0f);
		}
	}

	for (const TWeakObjectPtr<UActorPhaseMembershipComponent>& MembershipPtr : Memberships)
	{
		const UActorPhaseMembershipComponent* Membership = MembershipPtr.Get();
		const APawn* Pawn = Membership ? Membership->GetViewedPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const FString Label = FString::Printf(TEXT("Phase: %s"), *JoinKeys(Membership->GetMembershipRef()));
		DrawDebugString(World, Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f), Label, nullptr, FColor::Yellow, 0.0f, true);
	}
#endif
}
