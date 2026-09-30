// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhasingIrisSubsystem.h"

#include "ActorPhaseComponent.h"
#include "ActorPhaseMembershipComponent.h"
#include "ActorPhaseSubsystem.h"
#include "ActorPhasingIrisGroupTable.h"
#include "ActorPhasingLibrary.h"
#include "ActorPhasingModule.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/Core/Connection/ConnectionHandle.h"

#if UE_WITH_IRIS
#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Iris/ReplicationSystem/NetObjectGroupHandle.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#endif

namespace ActorPhasingIrisPrivate
{
#if UE_WITH_IRIS
	/** The real backend: one Iris exclusion group per table group. */
	class FIrisGroupBackend final : public IActorPhasingIrisGroupBackend
	{
	public:
		FIrisGroupBackend(UReplicationSystem* InSystem, UObjectReplicationBridge* InBridge)
			: System(InSystem)
			, Bridge(InBridge)
		{
		}

		virtual uint32 CreateGroup(const FString& DebugName) override
		{
			UReplicationSystem* Sys = System.Get();
			if (!Sys)
			{
				return 0;
			}
			const UE::Net::FNetObjectGroupHandle Handle = Sys->CreateGroup(FName(*DebugName));
			if (!Sys->IsValidGroup(Handle))
			{
				return 0;
			}
			if (!Sys->AddExclusionFilterGroup(Handle))
			{
				Sys->DestroyGroup(Handle);
				return 0;
			}
			// Exclusion groups start as "disallow for everyone"; the table opens them per connection.
			const uint32 Id = NextId++;
			Handles.Add(Id, Handle);
			return Id;
		}

		virtual void DestroyGroup(const uint32 GroupId) override
		{
			const UE::Net::FNetObjectGroupHandle* Found = Handles.Find(GroupId);
			if (!Found)
			{
				return;
			}
			const UE::Net::FNetObjectGroupHandle Handle = *Found;
			Handles.Remove(GroupId);
			if (UReplicationSystem* Sys = System.Get())
			{
				if (Sys->IsValidGroup(Handle))
				{
					Sys->RemoveGroupFilter(Handle);
					Sys->DestroyGroup(Handle);
				}
			}
		}

		virtual bool AddActorToGroup(const uint32 GroupId, AActor* Actor) override
		{
			UReplicationSystem* Sys = System.Get();
			UObjectReplicationBridge* ObjectBridge = Bridge.Get();
			const UE::Net::FNetObjectGroupHandle* Handle = Handles.Find(GroupId);
			if (!Sys || !ObjectBridge || !Handle)
			{
				return false;
			}
			const UE::Net::FNetRefHandle Ref = ObjectBridge->GetReplicatedRefHandle(Actor);
			if (!Ref.IsValid())
			{
				return false; // not replicating yet
			}
			Sys->AddToGroup(*Handle, Ref);
			return true;
		}

		virtual void RemoveActorFromGroup(const uint32 GroupId, AActor* Actor) override
		{
			UReplicationSystem* Sys = System.Get();
			UObjectReplicationBridge* ObjectBridge = Bridge.Get();
			const UE::Net::FNetObjectGroupHandle* Handle = Handles.Find(GroupId);
			if (!Sys || !ObjectBridge || !Handle || !Sys->IsValidGroup(*Handle))
			{
				return;
			}
			const UE::Net::FNetRefHandle Ref = ObjectBridge->GetReplicatedRefHandle(Actor);
			if (Ref.IsValid())
			{
				Sys->RemoveFromGroup(*Handle, Ref);
			}
		}

		virtual void SetGroupStatus(const uint32 GroupId, const uint32 ConnectionId, const bool bAllow) override
		{
			UReplicationSystem* Sys = System.Get();
			const UE::Net::FNetObjectGroupHandle* Handle = Handles.Find(GroupId);
			if (!Sys || !Handle || !Sys->IsValidGroup(*Handle) || !Sys->IsValidConnection(ConnectionId))
			{
				return;
			}
			Sys->SetGroupFilterStatus(*Handle, ConnectionId, bAllow ? UE::Net::ENetFilterStatus::Allow : UE::Net::ENetFilterStatus::Disallow);
		}

	private:
		TWeakObjectPtr<UReplicationSystem> System;
		TWeakObjectPtr<UObjectReplicationBridge> Bridge;
		TMap<uint32, UE::Net::FNetObjectGroupHandle> Handles;
		uint32 NextId = 1;
	};
#endif // UE_WITH_IRIS

	static uint32 ConnectionIdOf(const UNetConnection* Connection)
	{
		const UE::Net::FConnectionHandle Handle = Connection->GetConnectionHandle();
		return Handle.IsValid() ? Handle.GetParentConnectionId() : 0;
	}
}

// ---------------------------------------------------------------------------------------------
// Subsystem lifecycle
// ---------------------------------------------------------------------------------------------

bool UActorPhasingIrisSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UActorPhasingIrisSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UActorPhaseSubsystem>();
	Super::Initialize(Collection);
}

void UActorPhasingIrisSubsystem::Deinitialize()
{
	Unbind();
	Super::Deinitialize();
}

TStatId UActorPhasingIrisSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UActorPhasingIrisSubsystem, STATGROUP_Tickables);
}

ETickableTickType UActorPhasingIrisSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

void UActorPhasingIrisSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	const bool bIrisServer = Driver && Driver->IsUsingIrisReplication() && Driver->GetNetMode() != NM_Client;
	if (!bIrisServer)
	{
		if (Table.IsValid())
		{
			Unbind();
		}
		return;
	}

	if (BoundDriver.Get() != Driver || !Table.IsValid())
	{
		BindToDriver(Driver);
		if (!Table.IsValid())
		{
			return;
		}
	}

	ReconcileConnections(Driver);
	Table->FlushPending();
}

// ---------------------------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------------------------

void UActorPhasingIrisSubsystem::BindToDriver(UNetDriver* Driver)
{
	Unbind();

#if UE_WITH_IRIS
	UReplicationSystem* System = Driver ? Driver->GetReplicationSystem() : nullptr;
	UObjectReplicationBridge* Bridge = System ? System->GetReplicationBridge() : nullptr;
	if (!System || !Bridge)
	{
		return;
	}

	Backend = MakeUnique<ActorPhasingIrisPrivate::FIrisGroupBackend>(System, Bridge);
	Table = MakeUnique<FActorPhasingIrisGroupTable>(*Backend);
	BoundDriver = Driver;

	if (UActorPhaseSubsystem* Phasing = UActorPhaseSubsystem::Get(GetWorld()))
	{
		KeysChangedHandle = Phasing->OnActorPhaseKeysChangedNative.AddUObject(this, &UActorPhasingIrisSubsystem::HandleActorPhaseKeysChanged);
	}
	SeedActors();
	UE_LOG(LogActorPhasing, Log, TEXT("Iris adapter bound to %s (%d phased actors seeded)."), *GetNameSafe(Driver), Table->GetActorCount());
#endif
}

void UActorPhasingIrisSubsystem::Unbind()
{
	if (UActorPhaseSubsystem* Phasing = UActorPhaseSubsystem::Get(GetWorld()))
	{
		Phasing->OnActorPhaseKeysChangedNative.Remove(KeysChangedHandle);
	}
	KeysChangedHandle.Reset();
	KnownConnections.Empty();
	Table.Reset();
	Backend.Reset();
	BoundDriver.Reset();
}

void UActorPhasingIrisSubsystem::SeedActors()
{
	const UActorPhaseSubsystem* Phasing = UActorPhaseSubsystem::Get(GetWorld());
	if (!Phasing || !Table.IsValid())
	{
		return;
	}
	Phasing->ForEachPhasedActor([this, Phasing](AActor* Actor, const TArray<FActorPhaseKey>& Keys)
	{
		const UActorPhaseComponent* Component = Phasing->FindPhaseComponent(Actor);
		Table->SetActorKeys(Actor, Keys, Component && Component->IsVisibleToObservers());
	});
}

void UActorPhasingIrisSubsystem::ReconcileConnections(UNetDriver* Driver)
{
	const UActorPhaseSubsystem* Phasing = UActorPhaseSubsystem::Get(GetWorld());

	TSet<uint32> Seen;
	for (UNetConnection* Connection : Driver->ClientConnections)
	{
		if (!Connection)
		{
			continue;
		}
		const uint32 ConnectionId = ActorPhasingIrisPrivate::ConnectionIdOf(Connection);
		if (ConnectionId == 0)
		{
			continue;
		}
		Seen.Add(ConnectionId);

		const AActor* Viewer = Connection->PlayerController.Get();
		if (!Viewer)
		{
			Viewer = Connection->OwningActor.Get();
		}
		const UActorPhaseMembershipComponent* Membership = (Phasing && Viewer) ? Phasing->FindMembership(Viewer) : nullptr;
		static const TArray<FActorPhaseKey> NoObservedPhases;
		Table->SetConnectionView(ConnectionId,
			Membership ? Membership->GetMembershipRef() : UActorPhasingLibrary::GetBaseMembership(),
			Membership ? Membership->GetObservedPhasesRef() : NoObservedPhases);
	}

	for (const uint32 Known : KnownConnections)
	{
		if (!Seen.Contains(Known))
		{
			Table->RemoveConnection(Known);
		}
	}
	KnownConnections = MoveTemp(Seen);
}

void UActorPhasingIrisSubsystem::HandleActorPhaseKeysChanged(AActor* Actor, const TArray<FActorPhaseKey>& OldKeys, const TArray<FActorPhaseKey>& NewKeys)
{
	if (!Table.IsValid() || !Actor)
	{
		return;
	}
	if (NewKeys.Num() == 0 || !IsValid(Actor) || Actor->IsActorBeingDestroyed())
	{
		Table->RemoveActor(Actor);
		return;
	}
	const UActorPhaseSubsystem* Phasing = UActorPhaseSubsystem::Get(GetWorld());
	const UActorPhaseComponent* Component = Phasing ? Phasing->FindPhaseComponent(Actor) : nullptr;
	Table->SetActorKeys(Actor, NewKeys, Component && Component->IsVisibleToObservers());
}

FString UActorPhasingIrisSubsystem::DescribeState() const
{
	return Table.IsValid() ? Table->Describe() : FString(TEXT("Iris adapter inactive (net driver is not using Iris)."));
}
