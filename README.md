# Actor Phasing

WoW-style phasing for multiplayer Unreal Engine games. Players standing in the same place can
see different versions of the world: their own housing yard, their own stage of a quest, their
own instance of an event. Visibility is enforced at the replication level, cross-phase pawns
neither collide nor interact, and unviewed phases tell you when to despawn them.

- Engine: Unreal Engine 5.8. Works on the standard replication driver as is; optional modules
  adapt it to Replication Graph (`ActorPhasingReplicationGraph`) and Iris (`ActorPhasingIris`)
- Platforms: Win64, Linux, Mac (dedicated servers, listen servers, standalone)
- No engine modifications, no third-party code, Blueprint-first API

## Concepts

| Term | Meaning |
|---|---|
| **Phase key** | `{Domain, Id}`, e.g. `{"Housing", 4127}`. Invalid key = no phase. |
| **Base phase** | `Base:-1`. The shared world every viewer starts in. |
| **Membership** | The set of phases a viewer can see. Defaults to `{Base}`. |
| **Phased actor** | An actor with an `Actor Phase` component holding one or more keys. |
| **Unphased actor** | No component or no valid key: in every phase, visible to everyone. |
| **Observed phase** | A phase whose world content a viewer sees without joining it. |

**Rule**: a viewer sees a phased actor when its membership intersects the actor's keys, or when
the viewer owns the actor. `SetMembership` *replaces* the set, so a player moved into phase X no
longer sees base-phase players, and they no longer see the player (symmetric). Unphased world
actors (terrain, NPCs, buildings) stay visible to all.

**Observation**: `AddObservedPhase(X)` on a membership lets the viewer also see X's world content
(actors whose `Actor Phase` component is visible to observers, the default) while staying in its
own phase. Players in X stay hidden, the observer's pawn does not change, and interaction still
requires membership. Use it to show a phase from a distance before the viewer walks into it.

## Quick start

1. Enable the plugin and add `"ActorPhasing"` to your module's `PublicDependencyModuleNames`.
2. Add an **Actor Phase Membership** component to your PlayerState class (or PlayerController).
   The possessed pawn automatically receives an **Actor Phase** component that mirrors it.
3. Make phased actor classes consult the rule from `IsNetRelevantFor`:

```cpp
bool AMyFurniture::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	if (!UActorPhasingLibrary::IsActorRelevantToViewer(this, RealViewer))
	{
		return false;
	}
	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}
```

   Do the same on your player pawn class, and give phased actors an **Actor Phase** component
   (or derive from `APhasedActor`). Keep phased actors `DORM_Awake`.
4. Place an **Actor Phase Volume** where phasing should apply. Decide the phase per viewer by
   overriding `ResolvePhaseForViewer` (C++ or Blueprint), assigning a `Resolver` object, or
   setting `FixedPhase`. Entering applies the phase; leaving returns the viewer to base.
5. Spawn a phase's actors on the server with `SetSinglePhase(Key)` on their component and
   `SetCollisionScope(Volume)`; register `RegisterIdleRule(Key, GraceSeconds)` and bind
   `OnPhaseUnviewed` on the world subsystem to despawn them when nobody is left.
6. Gate server-side interaction and targeting with
   `UActorPhasingLibrary::CanViewerInteractWithActor(Controller, Target)`.

## What the plugin does for you

- **Immediate switches**: when a membership or key set changes, the subsystem closes the actor
  channels that are no longer relevant instead of waiting for the engine's 5-second
  `RelevantTimeout` (`bCloseStaleChannelsImmediately`).
- **Collision**: pawns registered in a collision scope (volume occupants) stop colliding with
  pawns and phased actors of other phases (`IgnoreActorWhenMoving`, movement sweeps only).
  Filter your own overlap and trace queries with `AreActorsInCompatiblePhases`.
- **Inheritance**: an unphased actor owned by a phased actor (projectile, pet) or attached to
  one inherits its phase, as long as its class consults the rule.
- **Diagnostics**: `ActorPhasing.Debug.Draw 1`, `ActorPhasing.Debug.Log 1`, `ActorPhasing.Dump`.
- **Settings**: Project Settings → Plugins → Actor Phasing.

## Replication drivers

The rule (membership ∩ keys ≠ ∅, or owner) is the same everywhere; what differs is where the
engine asks for it.

| Driver | How phasing is applied | What you do |
|---|---|---|
| Standard (default) | Your classes' `IsNetRelevantFor` + immediate channel closing | Quick start above |
| Replication Graph | `UActorPhasingReplicationGraphNode` gathers phased actors per phase | Use `UActorPhasingReplicationGraph` as your driver, or add the node to your own graph |
| Iris | `UActorPhasingIrisSubsystem` keeps one exclusion group per key set in sync with memberships | Nothing: it activates when the net driver uses Iris |

Replication Graph and Iris never call `IsNetRelevantFor`, so the overrides from the quick start are
harmless but inert under them; the adapters do the filtering. Both adapter modules are optional:
leave them out of your module's dependencies if you do not use them (they load with the plugin and
stay idle). See `Docs/ActorPhasing.md` §3 for configuration and limits.

## Limitations

- Only classes that call the rule are phased on the standard driver. Engine actors you do not
  subclass stay visible. (Under Replication Graph and Iris every actor with keys is filtered.)
- `PlayerState` actors are always relevant by engine design; names and scores replicate to
  everyone even when the pawn is hidden.
- Local chat, proximity voice and similar systems that use pawn relevancy become phase-scoped
  automatically; systems that iterate all pawns do not.

See `Docs/ActorPhasing.md` for the full reference.
