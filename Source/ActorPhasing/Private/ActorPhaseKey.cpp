// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#include "ActorPhaseKey.h"

const FName FActorPhaseKey::BaseDomain(TEXT("Base"));

FActorPhaseKey FActorPhaseKey::Base()
{
	return Make(BaseDomain, -1);
}

FActorPhaseKey FActorPhaseKey::Make(const FName InDomain, const int64 InId)
{
	FActorPhaseKey Key;
	Key.Domain = InDomain;
	Key.Id = InId;
	return Key;
}

FString FActorPhaseKey::ToString() const
{
	if (!IsValid())
	{
		return TEXT("None");
	}
	return FString::Printf(TEXT("%s:%lld"), *Domain.ToString(), Id);
}
