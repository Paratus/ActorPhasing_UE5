// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhasingIrisGroupTable.h"

#include "ActorPhasingLibrary.h"
#include "ActorPhasingModule.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

// ---------------------------------------------------------------------------------------------
// Key set
// ---------------------------------------------------------------------------------------------

FActorPhasingIrisKeySet FActorPhasingIrisKeySet::From(const TArray<FActorPhaseKey>& InKeys, const bool bInVisibleToObservers)
{
	FActorPhasingIrisKeySet Set;
	Set.bVisibleToObservers = bInVisibleToObservers;
	for (const FActorPhaseKey& Key : InKeys)
	{
		if (Key.IsValid())
		{
			Set.Keys.AddUnique(Key);
		}
	}
	Set.Keys.Sort([](const FActorPhaseKey& A, const FActorPhaseKey& B)
	{
		const int32 DomainOrder = A.Domain.Compare(B.Domain);
		return DomainOrder != 0 ? DomainOrder < 0 : A.Id < B.Id;
	});
	return Set;
}

bool FActorPhasingIrisKeySet::Intersects(const TArray<FActorPhaseKey>& Membership) const
{
	return UActorPhasingLibrary::KeysIntersect(Keys, Membership);
}

bool FActorPhasingIrisKeySet::AllowsViewer(const TArray<FActorPhaseKey>& Membership, const TArray<FActorPhaseKey>& Observed) const
{
	return Intersects(Membership) || (bVisibleToObservers && Intersects(Observed));
}

FString FActorPhasingIrisKeySet::ToString() const
{
	TArray<FString> Parts;
	for (const FActorPhaseKey& Key : Keys)
	{
		Parts.Add(Key.ToString());
	}
	return FString::Join(Parts, TEXT("+")) + (bVisibleToObservers ? TEXT(" (observable)") : TEXT(""));
}

// ---------------------------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------------------------

FActorPhasingIrisGroupTable::FActorPhasingIrisGroupTable(IActorPhasingIrisGroupBackend& InBackend)
	: Backend(InBackend)
{
}

FActorPhasingIrisGroupTable::~FActorPhasingIrisGroupTable()
{
	Reset();
}

FActorPhasingIrisGroupTable::FGroup* FActorPhasingIrisGroupTable::FindOrCreateGroup(const FActorPhasingIrisKeySet& KeySet)
{
	if (FGroup* Existing = Groups.Find(KeySet))
	{
		return Existing;
	}

	const uint32 Id = Backend.CreateGroup(FString::Printf(TEXT("ActorPhasing.%s"), *KeySet.ToString()));
	if (Id == 0)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextCreateFailureLogTime)
		{
			NextCreateFailureLogTime = Now + 5.0;
			UE_LOG(LogActorPhasing, Error, TEXT("Iris: could not create a filter group for phase set [%s]; its actors stay pending. Check the replication system's group limit."), *KeySet.ToString());
		}
		return nullptr;
	}

	FGroup& Group = Groups.Add(KeySet);
	Group.Id = Id;
	Group.KeySet = KeySet;
	ApplyStatusesForGroup(Group);
	return &Group;
}

void FActorPhasingIrisGroupTable::ReleaseGroupIfEmpty(const FActorPhasingIrisKeySet& KeySet)
{
	const FGroup* Group = Groups.Find(KeySet);
	if (!Group || Group->Members.Num() > 0)
	{
		return;
	}
	// Keep the group while an actor of this set is still waiting for its handle.
	for (const TObjectKey<AActor>& PendingKey : Pending)
	{
		if (const FActorEntry* Entry = Actors.Find(PendingKey))
		{
			if (Entry->KeySet == KeySet)
			{
				return;
			}
		}
	}
	Backend.DestroyGroup(Group->Id);
	Groups.Remove(KeySet);
}

void FActorPhasingIrisGroupTable::ApplyStatusesForGroup(const FGroup& Group) const
{
	for (const TPair<uint32, FConnectionView>& Connection : Connections)
	{
		Backend.SetGroupStatus(Group.Id, Connection.Key, Group.KeySet.AllowsViewer(Connection.Value.Membership, Connection.Value.Observed));
	}
}

void FActorPhasingIrisGroupTable::ApplyStatusesForConnection(const uint32 ConnectionId, const FConnectionView& View) const
{
	for (const TPair<FActorPhasingIrisKeySet, FGroup>& Pair : Groups)
	{
		Backend.SetGroupStatus(Pair.Value.Id, ConnectionId, Pair.Key.AllowsViewer(View.Membership, View.Observed));
	}
}

bool FActorPhasingIrisGroupTable::TryFile(AActor* Actor, FActorEntry& Entry)
{
	FGroup* Group = FindOrCreateGroup(Entry.KeySet);
	if (!Group || !Backend.AddActorToGroup(Group->Id, Actor))
	{
		Pending.Add(Actor);
		return false;
	}
	Group->Members.Add(Actor);
	Entry.bInGroup = true;
	Pending.Remove(Actor);
	return true;
}

void FActorPhasingIrisGroupTable::Unfile(AActor* Actor, FActorEntry& Entry)
{
	Pending.Remove(Actor);
	if (FGroup* Group = Groups.Find(Entry.KeySet))
	{
		if (Entry.bInGroup)
		{
			Backend.RemoveActorFromGroup(Group->Id, Actor);
			Group->Members.Remove(Actor);
		}
	}
	Entry.bInGroup = false;
	ReleaseGroupIfEmpty(Entry.KeySet);
}

void FActorPhasingIrisGroupTable::SetActorKeys(AActor* Actor, const TArray<FActorPhaseKey>& Keys, const bool bVisibleToObservers)
{
	if (!Actor)
	{
		return;
	}
	const FActorPhasingIrisKeySet KeySet = FActorPhasingIrisKeySet::From(Keys, bVisibleToObservers);
	if (KeySet.IsEmpty())
	{
		RemoveActor(Actor);
		return;
	}

	if (FActorEntry* Existing = Actors.Find(Actor))
	{
		if (Existing->KeySet == KeySet)
		{
			return;
		}
		Unfile(Actor, *Existing);
		Existing->KeySet = KeySet;
		TryFile(Actor, *Existing);
		return;
	}

	FActorEntry& Entry = Actors.Add(Actor);
	Entry.KeySet = KeySet;
	TryFile(Actor, Entry);
}

void FActorPhasingIrisGroupTable::RemoveActor(AActor* Actor)
{
	FActorEntry* Entry = Actor ? Actors.Find(Actor) : nullptr;
	if (!Entry)
	{
		return;
	}
	Unfile(Actor, *Entry);
	Actors.Remove(Actor);
}

void FActorPhasingIrisGroupTable::SetConnectionMembership(const uint32 ConnectionId, const TArray<FActorPhaseKey>& Membership)
{
	const FConnectionView* Existing = Connections.Find(ConnectionId);
	SetConnectionView(ConnectionId, Membership, Existing ? Existing->Observed : TArray<FActorPhaseKey>());
}

void FActorPhasingIrisGroupTable::SetConnectionView(const uint32 ConnectionId, const TArray<FActorPhaseKey>& Membership, const TArray<FActorPhaseKey>& Observed)
{
	FConnectionView* View = Connections.Find(ConnectionId);
	if (View)
	{
		if (View->Membership == Membership && View->Observed == Observed)
		{
			return;
		}
		View->Membership = Membership;
		View->Observed = Observed;
	}
	else
	{
		View = &Connections.Add(ConnectionId, FConnectionView{ Membership, Observed });
	}
	ApplyStatusesForConnection(ConnectionId, *View);
}

void FActorPhasingIrisGroupTable::RemoveConnection(const uint32 ConnectionId)
{
	// Iris drops per-connection group state with the connection; nothing to undo on the backend.
	Connections.Remove(ConnectionId);
}

void FActorPhasingIrisGroupTable::FlushPending()
{
	if (Pending.Num() == 0)
	{
		return;
	}
	TArray<TObjectKey<AActor>> Snapshot = Pending.Array();
	for (const TObjectKey<AActor>& Key : Snapshot)
	{
		AActor* Actor = Key.ResolveObjectPtr();
		if (!IsValid(Actor))
		{
			Pending.Remove(Key);
			Actors.Remove(Key);
			continue;
		}
		if (FActorEntry* Entry = Actors.Find(Key))
		{
			TryFile(Actor, *Entry);
		}
		else
		{
			Pending.Remove(Key);
		}
	}
}

void FActorPhasingIrisGroupTable::Reset()
{
	for (const TPair<FActorPhasingIrisKeySet, FGroup>& Pair : Groups)
	{
		Backend.DestroyGroup(Pair.Value.Id);
	}
	Groups.Empty();
	Actors.Empty();
	Connections.Empty();
	Pending.Empty();
}

bool FActorPhasingIrisGroupTable::IsActorGrouped(const AActor* Actor) const
{
	const FActorEntry* Entry = Actors.Find(Actor);
	return Entry && Entry->bInGroup;
}

uint32 FActorPhasingIrisGroupTable::FindGroupIdForActor(const AActor* Actor) const
{
	const FActorEntry* Entry = Actors.Find(Actor);
	const FGroup* Group = Entry ? Groups.Find(Entry->KeySet) : nullptr;
	return Group ? Group->Id : 0;
}

bool FActorPhasingIrisGroupTable::IsActorAllowedForConnection(const AActor* Actor, const uint32 ConnectionId) const
{
	const FActorEntry* Entry = Actors.Find(Actor);
	if (!Entry)
	{
		return true; // not phased: unfiltered
	}
	const FConnectionView* View = Connections.Find(ConnectionId);
	return View ? Entry->KeySet.AllowsViewer(View->Membership, View->Observed) : false;
}

FString FActorPhasingIrisGroupTable::Describe() const
{
	FString Out = FString::Printf(TEXT("Iris phase groups: %d groups, %d actors (%d pending), %d connections\n"), Groups.Num(), Actors.Num(), Pending.Num(), Connections.Num());
	for (const TPair<FActorPhasingIrisKeySet, FGroup>& Pair : Groups)
	{
		Out += FString::Printf(TEXT("  [%s] group %u: %d actors\n"), *Pair.Key.ToString(), Pair.Value.Id, Pair.Value.Members.Num());
	}
	return Out;
}
