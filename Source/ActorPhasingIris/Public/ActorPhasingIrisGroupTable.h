// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseKey.h"
#include "UObject/ObjectKey.h"

class AActor;

/**
 * The Iris calls the group table needs, behind an interface so the bookkeeping can be tested
 * without a replication system. Group ids are table-local; 0 means "could not create".
 */
class ACTORPHASINGIRIS_API IActorPhasingIrisGroupBackend
{
public:
	virtual ~IActorPhasingIrisGroupBackend() = default;

	/** Creates an exclusion group that disallows every connection until told otherwise. Returns 0 on failure. */
	virtual uint32 CreateGroup(const FString& DebugName) = 0;
	virtual void DestroyGroup(uint32 GroupId) = 0;

	/** Returns false when the actor is not replicating yet; the table retries on FlushPending. */
	virtual bool AddActorToGroup(uint32 GroupId, AActor* Actor) = 0;
	virtual void RemoveActorFromGroup(uint32 GroupId, AActor* Actor) = 0;

	virtual void SetGroupStatus(uint32 GroupId, uint32 ConnectionId, bool bAllow) = 0;
};

/**
 * A sorted, de-duplicated set of valid phase keys plus whether observers of those phases may see
 * its actors, usable as a map key.
 */
struct ACTORPHASINGIRIS_API FActorPhasingIrisKeySet
{
	TArray<FActorPhaseKey> Keys;
	bool bVisibleToObservers = false;

	static FActorPhasingIrisKeySet From(const TArray<FActorPhaseKey>& InKeys, bool bInVisibleToObservers = false);

	bool IsEmpty() const { return Keys.Num() == 0; }
	bool Intersects(const TArray<FActorPhaseKey>& Membership) const;
	/** The core rule: membership intersects the keys, or the viewer observes one and observers may see the set. */
	bool AllowsViewer(const TArray<FActorPhaseKey>& Membership, const TArray<FActorPhaseKey>& Observed) const;
	FString ToString() const;

	bool operator==(const FActorPhasingIrisKeySet& Other) const { return Keys == Other.Keys && bVisibleToObservers == Other.bVisibleToObservers; }
	bool operator!=(const FActorPhasingIrisKeySet& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FActorPhasingIrisKeySet& Set)
	{
		uint32 Hash = Set.bVisibleToObservers ? 1u : 0u;
		for (const FActorPhaseKey& Key : Set.Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(Key));
		}
		return Hash;
	}
};

/**
 * Maps phased actors to one Iris exclusion group per distinct key set and keeps each group's
 * per-connection status equal to "the connection's membership intersects the key set, or the
 * connection observes one of the keys and the set is visible to observers".
 *
 * A connection therefore receives a phased actor exactly when the core rule would make it
 * relevant (membership ∩ keys ≠ ∅, or observed ∩ keys ≠ ∅ for observable actors). Groups are created on demand and destroyed when their last
 * actor leaves, which keeps the count well under Iris' group limit for ordinary use.
 */
class ACTORPHASINGIRIS_API FActorPhasingIrisGroupTable
{
public:
	explicit FActorPhasingIrisGroupTable(IActorPhasingIrisGroupBackend& InBackend);
	~FActorPhasingIrisGroupTable();

	/** Files the actor under its keys. Empty keys remove it. Idempotent for unchanged keys. */
	void SetActorKeys(AActor* Actor, const TArray<FActorPhaseKey>& Keys, bool bVisibleToObservers = false);
	void RemoveActor(AActor* Actor);

	/** Records a connection's membership (keeping its observed phases) and updates every group's status for it. Idempotent. */
	void SetConnectionMembership(uint32 ConnectionId, const TArray<FActorPhaseKey>& Membership);

	/** Records a connection's membership and observed phases and updates every group's status for it. Idempotent. */
	void SetConnectionView(uint32 ConnectionId, const TArray<FActorPhaseKey>& Membership, const TArray<FActorPhaseKey>& Observed);
	void RemoveConnection(uint32 ConnectionId);

	/** Retries actors that had no replication handle when they were filed. */
	void FlushPending();

	/** Drops all bookkeeping and destroys every group. */
	void Reset();

	int32 GetGroupCount() const { return Groups.Num(); }
	int32 GetActorCount() const { return Actors.Num(); }
	int32 GetPendingCount() const { return Pending.Num(); }
	int32 GetConnectionCount() const { return Connections.Num(); }

	/** True once the actor has been added to its group (not pending). */
	bool IsActorGrouped(const AActor* Actor) const;

	/** The table-local group id of the actor's key set, or 0. */
	uint32 FindGroupIdForActor(const AActor* Actor) const;

	/** Whether the connection is currently allowed to receive the actor. */
	bool IsActorAllowedForConnection(const AActor* Actor, uint32 ConnectionId) const;

	FString Describe() const;

private:
	struct FGroup
	{
		uint32 Id = 0;
		FActorPhasingIrisKeySet KeySet;
		TSet<TObjectKey<AActor>> Members;
	};

	struct FActorEntry
	{
		FActorPhasingIrisKeySet KeySet;
		bool bInGroup = false;
	};

	struct FConnectionView
	{
		TArray<FActorPhaseKey> Membership;
		TArray<FActorPhaseKey> Observed;
	};

	FGroup* FindOrCreateGroup(const FActorPhasingIrisKeySet& KeySet);
	void ReleaseGroupIfEmpty(const FActorPhasingIrisKeySet& KeySet);
	void ApplyStatusesForGroup(const FGroup& Group) const;
	void ApplyStatusesForConnection(uint32 ConnectionId, const FConnectionView& View) const;
	void Unfile(AActor* Actor, FActorEntry& Entry);
	bool TryFile(AActor* Actor, FActorEntry& Entry);

	IActorPhasingIrisGroupBackend& Backend;
	TMap<FActorPhasingIrisKeySet, FGroup> Groups;
	TMap<TObjectKey<AActor>, FActorEntry> Actors;
	TMap<uint32, FConnectionView> Connections;
	TSet<TObjectKey<AActor>> Pending;
	double NextCreateFailureLogTime = 0.0;
};
