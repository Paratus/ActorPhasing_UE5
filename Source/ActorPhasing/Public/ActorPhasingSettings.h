// Copyright (c) 2026 Artisan Gameworks Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ActorPhasingSettings.generated.h"

/**
 * Project-wide defaults for Actor Phasing.
 * Project Settings -> Plugins -> Actor Phasing, or [/Script/ActorPhasing.ActorPhasingSettings] in DefaultGame.ini.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Actor Phasing"))
class ACTORPHASING_API UActorPhasingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UActorPhasingSettings();

	virtual FName GetCategoryName() const override;

	/** Grace period used by RegisterIdleRule when the caller passes a negative value. */
	UPROPERTY(config, EditAnywhere, Category = "Lifecycle", meta = (ClampMin = "0.0", Units = "Seconds"))
	float DefaultIdleGraceSeconds = 60.0f;

	/**
	 * Close an actor's channel to a connection as soon as it stops being relevant to that viewer.
	 * When false the engine's RelevantTimeout (5 s by default) applies and a switched-out actor
	 * lingers on the client until then.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Replication")
	bool bCloseStaleChannelsImmediately = true;

	/**
	 * Keep pawns from colliding with actors and pawns of other phases inside a collision scope.
	 * Phase volumes register their occupants as a scope automatically.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Collision")
	bool bMaintainMoveIgnore = true;

	/** Phase volumes add a membership component to a PlayerState that has none. */
	UPROPERTY(config, EditAnywhere, Category = "Volumes")
	bool bAutoAddMembershipComponent = true;

	/** Membership components add a phase component to a possessed pawn that has none. */
	UPROPERTY(config, EditAnywhere, Category = "Pawns")
	bool bAutoAddPawnPhaseComponent = true;

	/**
	 * How many owner links an unphased actor follows to inherit a phase (projectile -> pawn).
	 * 0 disables owner inheritance.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Rules", meta = (ClampMin = "0", ClampMax = "8"))
	int32 OwnerChainDepth = 3;

	/** An unphased actor attached to a phased actor inherits its phase. */
	UPROPERTY(config, EditAnywhere, Category = "Rules")
	bool bInheritPhaseFromAttachParent = true;

	/** Seconds between re-checks for viewers that have no membership component (negative cache). */
	UPROPERTY(config, EditAnywhere, Category = "Rules", meta = (ClampMin = "0.1", Units = "Seconds"))
	float ViewerLookupRecheckSeconds = 2.0f;
};
