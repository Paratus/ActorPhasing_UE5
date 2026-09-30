// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhasingLibrary.h"

#include "ActorPhaseComponent.h"
#include "ActorPhaseInterfaces.h"
#include "ActorPhaseMembershipComponent.h"
#include "ActorPhaseSubsystem.h"
#include "ActorPhasingSettings.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace ActorPhasingLibraryPrivate
{
	const TArray<FActorPhaseKey>* KeysFromComponent(const UActorPhaseComponent* Component)
	{
		return (Component && Component->IsPhased()) ? &Component->GetPhaseKeysRef() : nullptr;
	}

	UActorPhaseComponent* FindComponentOn(const AActor* Actor, const UActorPhaseSubsystem* Subsystem)
	{
		if (!Actor)
		{
			return nullptr;
		}
		if (Subsystem)
		{
			if (UActorPhaseComponent* Registered = Subsystem->FindPhaseComponent(Actor))
			{
				return Registered;
			}
		}
		// Interface casts need a mutable object pointer; the interface method itself is const.
		if (const IPhasedActorInterface* Phased = Cast<IPhasedActorInterface>(const_cast<AActor*>(Actor)))
		{
			return Phased->GetActorPhaseComponent();
		}
		if (!Subsystem)
		{
			// No registry (clients, editor worlds): fall back to a component search.
			return Actor->FindComponentByClass<UActorPhaseComponent>();
		}
		return nullptr;
	}

	UActorPhaseMembershipComponent* FindMembershipSlow(const AActor* Viewer)
	{
		if (!Viewer)
		{
			return nullptr;
		}
		if (const IActorPhaseViewerInterface* Interface = Cast<IActorPhaseViewerInterface>(const_cast<AActor*>(Viewer)))
		{
			return Interface->GetActorPhaseMembership();
		}
		if (const APlayerController* Controller = Cast<APlayerController>(Viewer))
		{
			if (UActorPhaseMembershipComponent* OnController = Controller->FindComponentByClass<UActorPhaseMembershipComponent>())
			{
				return OnController;
			}
			return Controller->PlayerState ? Controller->PlayerState->FindComponentByClass<UActorPhaseMembershipComponent>() : nullptr;
		}
		if (const APlayerState* PlayerState = Cast<APlayerState>(Viewer))
		{
			if (UActorPhaseMembershipComponent* OnState = PlayerState->FindComponentByClass<UActorPhaseMembershipComponent>())
			{
				return OnState;
			}
			const APlayerController* StateController = PlayerState->GetPlayerController();
			return StateController ? StateController->FindComponentByClass<UActorPhaseMembershipComponent>() : nullptr;
		}
		if (const APawn* Pawn = Cast<APawn>(Viewer))
		{
			if (const APlayerState* PawnState = Pawn->GetPlayerState())
			{
				return FindMembershipSlow(PawnState);
			}
			if (const AController* PawnController = Pawn->GetController())
			{
				return PawnController->FindComponentByClass<UActorPhaseMembershipComponent>();
			}
		}
		return nullptr;
	}
}

bool UActorPhasingLibrary::IsActorRelevantToViewer(const AActor* Actor, const AActor* RealViewer)
{
	return EvaluateRelevancy(Actor, RealViewer, /*bIncludeObservation*/ true);
}

bool UActorPhasingLibrary::IsActorInViewerPhase(const AActor* Actor, const AActor* Viewer)
{
	return EvaluateRelevancy(Actor, Viewer, /*bIncludeObservation*/ false);
}

bool UActorPhasingLibrary::EvaluateRelevancy(const AActor* Actor, const AActor* Viewer, const bool bIncludeObservation)
{
	if (!Actor || !Viewer)
	{
		return true;
	}

	const UActorPhaseSubsystem* Subsystem = UActorPhaseSubsystem::Get(Actor);
	const UActorPhaseComponent* Source = ResolveEffectivePhaseComponent(Actor, Subsystem);
	if (!Source)
	{
		return true; // unphased: every phase
	}

	if (IsActorOwnedByViewer(Actor, Viewer))
	{
		return true;
	}

	const TArray<FActorPhaseKey>& Keys = Source->GetPhaseKeysRef();
	const UActorPhaseMembershipComponent* Membership = Subsystem
		? Subsystem->FindMembership(Viewer)
		: ActorPhasingLibraryPrivate::FindMembershipSlow(Viewer);
	const TArray<FActorPhaseKey>& ViewerKeys = Membership ? Membership->GetMembershipRef() : GetBaseMembership();
	if (KeysIntersect(ViewerKeys, Keys))
	{
		return true;
	}

	return bIncludeObservation && Membership && Source->IsVisibleToObservers()
		&& KeysIntersect(Membership->GetObservedPhasesRef(), Keys);
}

bool UActorPhasingLibrary::AreActorsInCompatiblePhases(const AActor* A, const AActor* B)
{
	if (!A || !B || A == B)
	{
		return true;
	}
	const UActorPhaseSubsystem* Subsystem = UActorPhaseSubsystem::Get(A);
	const TArray<FActorPhaseKey>* KeysA = ResolveEffectivePhaseKeys(A, Subsystem);
	const TArray<FActorPhaseKey>* KeysB = ResolveEffectivePhaseKeys(B, Subsystem);
	if (!KeysA || !KeysB)
	{
		return true;
	}
	return KeysIntersect(*KeysA, *KeysB);
}

bool UActorPhasingLibrary::CanViewerInteractWithActor(const AActor* Viewer, const AActor* Target)
{
	return IsActorInViewerPhase(Target, Viewer);
}

bool UActorPhasingLibrary::GetEffectivePhaseKeys(const AActor* Actor, TArray<FActorPhaseKey>& OutKeys)
{
	OutKeys.Reset();
	const TArray<FActorPhaseKey>* Keys = ResolveEffectivePhaseKeys(Actor, UActorPhaseSubsystem::Get(Actor));
	if (!Keys)
	{
		return false;
	}
	OutKeys = *Keys;
	return true;
}

bool UActorPhasingLibrary::GetEffectivePhaseInfo(const AActor* Actor, TArray<FActorPhaseKey>& OutKeys, bool& bOutVisibleToObservers)
{
	OutKeys.Reset();
	bOutVisibleToObservers = false;
	const UActorPhaseComponent* Source = ResolveEffectivePhaseComponent(Actor, UActorPhaseSubsystem::Get(Actor));
	if (!Source)
	{
		return false;
	}
	OutKeys = Source->GetPhaseKeysRef();
	bOutVisibleToObservers = Source->IsVisibleToObservers();
	return true;
}

bool UActorPhasingLibrary::GetViewerMembership(const AActor* Viewer, TArray<FActorPhaseKey>& OutMembership)
{
	OutMembership.Reset();
	const UActorPhaseMembershipComponent* Membership = FindMembershipForViewer(Viewer);
	if (!Membership)
	{
		OutMembership = GetBaseMembership();
		return false;
	}
	OutMembership = Membership->GetMembershipRef();
	return true;
}

UActorPhaseMembershipComponent* UActorPhasingLibrary::FindMembershipForViewer(const AActor* Viewer)
{
	if (!Viewer)
	{
		return nullptr;
	}
	if (const UActorPhaseSubsystem* Subsystem = UActorPhaseSubsystem::Get(Viewer))
	{
		return Subsystem->FindMembership(Viewer);
	}
	return ActorPhasingLibraryPrivate::FindMembershipSlow(Viewer);
}

UActorPhaseComponent* UActorPhasingLibrary::FindPhaseComponent(const AActor* Actor)
{
	return ActorPhasingLibraryPrivate::FindComponentOn(Actor, UActorPhaseSubsystem::Get(Actor));
}

bool UActorPhasingLibrary::KeysIntersect(const TArray<FActorPhaseKey>& A, const TArray<FActorPhaseKey>& B)
{
	for (const FActorPhaseKey& KeyA : A)
	{
		if (!KeyA.IsValid())
		{
			continue;
		}
		for (const FActorPhaseKey& KeyB : B)
		{
			if (KeyA == KeyB)
			{
				return true;
			}
		}
	}
	return false;
}

FActorPhaseKey UActorPhasingLibrary::MakePhaseKey(const FName Domain, const int64 Id)
{
	return FActorPhaseKey::Make(Domain, Id);
}

FActorPhaseKey UActorPhasingLibrary::BasePhaseKey()
{
	return FActorPhaseKey::Base();
}

bool UActorPhasingLibrary::IsPhaseKeyValid(const FActorPhaseKey& Key)
{
	return Key.IsValid();
}

bool UActorPhasingLibrary::IsBasePhaseKey(const FActorPhaseKey& Key)
{
	return Key.IsBase();
}

bool UActorPhasingLibrary::PhaseKeysEqual(const FActorPhaseKey& A, const FActorPhaseKey& B)
{
	return A == B;
}

FString UActorPhasingLibrary::PhaseKeyToString(const FActorPhaseKey& Key)
{
	return Key.ToString();
}

const TArray<FActorPhaseKey>& UActorPhasingLibrary::GetBaseMembership()
{
	static const TArray<FActorPhaseKey> BaseMembership = { FActorPhaseKey::Base() };
	return BaseMembership;
}

const TArray<FActorPhaseKey>* UActorPhasingLibrary::ResolveEffectivePhaseKeys(const AActor* Actor, const UActorPhaseSubsystem* Subsystem)
{
	const UActorPhaseComponent* Source = ResolveEffectivePhaseComponent(Actor, Subsystem);
	return Source ? &Source->GetPhaseKeysRef() : nullptr;
}

const UActorPhaseComponent* UActorPhasingLibrary::ResolveEffectivePhaseComponent(const AActor* Actor, const UActorPhaseSubsystem* Subsystem)
{
	using namespace ActorPhasingLibraryPrivate;

	if (!Actor)
	{
		return nullptr;
	}

	const auto PhasedComponentOn = [Subsystem](const AActor* Candidate) -> const UActorPhaseComponent*
	{
		const UActorPhaseComponent* Component = FindComponentOn(Candidate, Subsystem);
		return KeysFromComponent(Component) ? Component : nullptr;
	};

	if (const UActorPhaseComponent* Own = PhasedComponentOn(Actor))
	{
		return Own;
	}

	const UActorPhasingSettings* Settings = GetDefault<UActorPhasingSettings>();

	const AActor* Cursor = Actor->GetOwner();
	for (int32 Depth = 0; Cursor && Depth < Settings->OwnerChainDepth; ++Depth)
	{
		if (const UActorPhaseComponent* OwnerComponent = PhasedComponentOn(Cursor))
		{
			return OwnerComponent;
		}
		Cursor = Cursor->GetOwner();
	}

	if (Settings->bInheritPhaseFromAttachParent)
	{
		if (const AActor* Parent = Actor->GetAttachParentActor())
		{
			if (const UActorPhaseComponent* ParentComponent = PhasedComponentOn(Parent))
			{
				return ParentComponent;
			}
		}
	}

	return nullptr;
}

bool UActorPhasingLibrary::IsActorOwnedByViewer(const AActor* Actor, const AActor* Viewer)
{
	if (!Actor || !Viewer)
	{
		return false;
	}
	if (Actor == Viewer || Actor->IsOwnedBy(Viewer))
	{
		return true;
	}

	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (Pawn->GetController() == Viewer || (Pawn->GetPlayerState() && Pawn->GetPlayerState() == Viewer))
		{
			return true;
		}
	}

	if (const APlayerController* Controller = Cast<APlayerController>(Viewer))
	{
		if (Actor == Controller->PlayerState || Actor == Controller->GetPawn())
		{
			return true;
		}
	}
	else if (const APlayerState* PlayerState = Cast<APlayerState>(Viewer))
	{
		if (Actor == PlayerState->GetPawn() || Actor == PlayerState->GetPlayerController())
		{
			return true;
		}
	}
	else if (const APawn* ViewerPawn = Cast<APawn>(Viewer))
	{
		if (Actor == ViewerPawn->GetController() || Actor == ViewerPawn->GetPlayerState())
		{
			return true;
		}
	}

	return false;
}
