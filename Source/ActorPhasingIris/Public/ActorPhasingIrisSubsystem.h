// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorPhaseKey.h"
#include "ActorPhasingIrisGroupTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "ActorPhasingIrisSubsystem.generated.h"

class UActorPhaseMembershipComponent;
class UActorPhaseSubsystem;
class UNetDriver;

/**
 * Keeps Iris group filters in sync with Actor Phasing on servers that replicate with Iris.
 *
 * Nothing happens until the world's net driver reports Iris replication; the subsystem then
 * creates the group table against that driver's replication system, seeds it with every phased
 * actor, and each tick reconciles connections (membership from the connection's PlayerController)
 * and retries actors that began replicating after they were filed. Projects on the standard driver
 * or on Replication Graph pay one null check per tick.
 *
 * The rule enforced is the core one: a connection receives a phased actor when its membership
 * intersects the actor's keys, or when it observes one of them and the actor is visible to
 * observers. Unphased actors are never touched. A viewer's own pawn mirrors its
 * membership and is therefore always allowed; other owned actors with disjoint keys are filtered.
 */
UCLASS()
class ACTORPHASINGIRIS_API UActorPhasingIrisSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ~USubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ~FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;

	/** True while bound to an Iris replication system. */
	UFUNCTION(BlueprintPure, Category = "Actor Phasing|Iris")
	bool IsActive() const { return Table.IsValid(); }

	/** Multi-line summary of groups, actors and connections. */
	UFUNCTION(BlueprintCallable, Category = "Actor Phasing|Iris")
	FString DescribeState() const;

	/** Table access for diagnostics and tests; null while inactive. */
	const FActorPhasingIrisGroupTable* GetTable() const { return Table.Get(); }

private:
	void BindToDriver(UNetDriver* Driver);
	void Unbind();
	void SeedActors();
	void ReconcileConnections(UNetDriver* Driver);

	void HandleActorPhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& OldKeys, const TArray<FActorPhaseKey>& NewKeys);

	TWeakObjectPtr<UNetDriver> BoundDriver;
	TUniquePtr<IActorPhasingIrisGroupBackend> Backend;
	TUniquePtr<FActorPhasingIrisGroupTable> Table;
	FDelegateHandle KeysChangedHandle;

	/** Connection ids seen during the last reconcile. */
	TSet<uint32> KnownConnections;
};
