// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"
#include "ActorPhaseKey.generated.h"

/**
 * Identifies one phase: a namespace (Domain) plus an identifier within it.
 *
 * - An invalid key (Domain == NAME_None) means "no phase". An actor whose phase component holds
 *   no valid key is in every phase and is visible to everyone.
 * - The base phase (FActorPhaseKey::Base()) is the shared world every viewer starts in.
 * - Consumers choose their own domains, for example {"Housing", HouseId} or {"Quest", StageId}.
 */
USTRUCT(BlueprintType)
struct ACTORPHASING_API FActorPhaseKey
{
	GENERATED_BODY()

	/** Namespace of the phase. NAME_None means "no phase". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Phasing")
	FName Domain;

	/** Identifier within the domain. The base phase uses -1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Phasing")
	int64 Id = -1;

	/** Domain name of the shared base phase. */
	static const FName BaseDomain;

	/** The shared base phase every viewer starts in. */
	static FActorPhaseKey Base();

	/** Builds a key from a domain and an id. */
	static FActorPhaseKey Make(FName InDomain, int64 InId);

	/** True when the key names a phase (the domain is set). */
	bool IsValid() const { return !Domain.IsNone(); }

	/** True for the shared base phase. */
	bool IsBase() const { return Domain == BaseDomain; }

	bool operator==(const FActorPhaseKey& Other) const { return Domain == Other.Domain && Id == Other.Id; }
	bool operator!=(const FActorPhaseKey& Other) const { return !(*this == Other); }

	/** "Domain:Id", or "None" for an invalid key. */
	FString ToString() const;

	friend uint32 GetTypeHash(const FActorPhaseKey& Key)
	{
		return HashCombine(GetTypeHash(Key.Domain), GetTypeHash(Key.Id));
	}
};

template <>
struct TStructOpsTypeTraits<FActorPhaseKey> : public TStructOpsTypeTraitsBase2<FActorPhaseKey>
{
	enum
	{
		WithIdenticalViaEquality = true,
	};
};
