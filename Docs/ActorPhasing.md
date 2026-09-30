# Actor Phasing — Reference

## 1. Model

A **phase key** is `{Domain: FName, Id: int64}`. `FActorPhaseKey::Base()` is `Base:-1`, the shared
world. An invalid key (`Domain == None`) means "no phase".

| Actor state | Effective keys | Visible to |
|---|---|---|
| No `Actor Phase` component, or no valid key | every phase | everyone |
| Component with keys `K` | `K` | viewers whose membership intersects `K`, plus the owner |
| No component but owned by / attached to a phased actor | the parent's keys (up to `OwnerChainDepth` links) | as above |
| Component with keys `K`, visible to observers (default for non-pawns) | `K` | as above, plus viewers whose **observed phases** intersect `K` |

A **viewer** is a connection. Its membership is the `Actor Phase Membership` component on its
PlayerState (recommended) or PlayerController. Without one it is treated as `{Base}`.

`SetMembership` replaces the set. Selecting phase X therefore means leaving base: a player in X and
a player in base do not see each other, do not collide inside a scope, and cannot interact. Both
still see every unphased actor. This is the WoW mask-intersection model.

The possessed pawn mirrors its owner's membership: the membership component binds
`APlayerState::OnPawnSet` (or `AController::OnPossessedPawnChanged`) and pushes its set onto the
pawn's `Actor Phase` component, adding one when missing (`bAutoAddPawnPhaseComponent`). A pawn that
is unpossessed keeps its last keys (a corpse stays in the phase it died in).

**Observed phases** are a second set on the membership component. A viewer sees the world content
of an observed phase without joining it: the actor's phase component must be visible to observers
(`bVisibleToObservers`, default true, always false while the component mirrors a membership). The
pawn never mirrors observed phases, so players in the phase and the observer still do not see each
other, and `CanViewerInteractWithActor` ignores observation. Idle rules count observers as viewers.
Typical use: preview a phase's props from a distance, then join the phase on entering its volume.

## 2. Classes

### `UActorPhaseComponent` (Actor Phase)
Server-side, not replicated. `SetPhaseKeys`, `SetSinglePhase`, `AddPhaseKey`, `RemovePhaseKey`,
`ClearPhaseKeys`, `IsPhased`, `HasPhaseKey`, `SetCollisionScope`, `IsVisibleToObservers`,
`SetVisibleToObservers`, `OnPhaseKeysChanged`.
Registers with the subsystem on register/begin play. Changing keys wakes the actor from dormancy and
closes stale channels.

### `UActorPhaseMembershipComponent` (Actor Phase Membership)
Replicated to the owning client only. `SetMembership`, `SetSinglePhase`, `ResetToBase`,
`GetMembership`, `HasPhase`, `IsInBasePhase`, `IntersectsKeys`, `GetOwningPlayerState`,
`GetOwningPlayerController`, `GetViewedPawn`, `OnMembershipChanged` (server and owning client).
Observation: `SetObservedPhases`, `AddObservedPhase`, `RemoveObservedPhase`, `ClearObservedPhases`,
`GetObservedPhases`, `IsObservingPhase`, `OnObservedPhasesChanged` (also owner-only replicated).

### `UActorPhasingLibrary`
`IsActorRelevantToViewer(Actor, RealViewer)` is the rule (observation included).
`IsActorInViewerPhase(Actor, Viewer)` is the rule without observation. `AreActorsInCompatiblePhases(A, B)`,
`CanViewerInteractWithActor(Viewer, Target)` (membership only), `GetEffectivePhaseKeys`,
`GetEffectivePhaseInfo` (keys plus observer visibility), `GetViewerMembership`,
`FindMembershipForViewer`, `FindPhaseComponent`, `KeysIntersect`, key helpers.

### `UActorPhaseSubsystem`
World subsystem (game and PIE worlds; on clients it only serves lookups).
- Queries: `GetViewerCount`, `GetObserverCount`, `GetActorCount`, `GetActorsInPhase`, `GetViewersInPhase`.
- Collision scopes: `RegisterPawnInCollisionScope`, `UnregisterPawnFromCollisionScope`,
  `RegisterActorInCollisionScope`, `UnregisterActorFromCollisionScope`, `RecomputeCollisionScope`.
- Channels: `RefreshActorChannels(Actor)`, `RefreshViewerChannels(Viewer)`.
- Idle rules: `RegisterIdleRule(Key, GraceSeconds)`, `ClearIdleRule`, `HasIdleRule`,
  `OnPhaseUnviewed`. A rule fires once and is removed; register again after respawning. Members
  and observers both keep a rule from firing.
- `DescribeState()` and the console command `ActorPhasing.Dump`.

### `AActorPhaseVolume` (Actor Phase Volume)
Level-placed box (WorldDynamic, query only, overlaps Pawn). Server-only overlaps with a PlayerState
retry for pawns that spawn inside. `ResolvePhaseForViewer(Viewer, OutKey)` (BlueprintNativeEvent):
default order is `Resolver` object (`ActorPhaseResolverInterface`) → `FixedPhase` → false (stay in
base). `ReapplyForPawn` / `ReapplyForPlayerState` / `ReapplyAll` re-run the resolution for
occupants. Options: `bResetToBaseOnExit`, `bMaintainCollisionScope`, retry attempts/interval.
Delegates `OnPawnEntered`, `OnPawnExited`. `GetWorldBounds`, `ContainsPoint`, `IsPawnInside`,
`GetOccupants`.

### `APhasedActor`
Replicated `AActor` with an `Actor Phase` component and the `IsNetRelevantFor` override. Optional;
existing hierarchies add the component and the override themselves.

### `UActorPhasingSettings`
`DefaultIdleGraceSeconds`, `bCloseStaleChannelsImmediately`, `bMaintainMoveIgnore`,
`bAutoAddMembershipComponent`, `bAutoAddPawnPhaseComponent`, `OwnerChainDepth`,
`bInheritPhaseFromAttachParent`, `ViewerLookupRecheckSeconds`.

## 3. Network model

The standard net driver asks every replicated actor `IsNetRelevantFor(RealViewer, ...)` per
connection, where `RealViewer` is the connection's PlayerController. Phased classes answer with the
rule first, then `Super`. `APawn::IsNetRelevantFor` and `AActor::IsNetRelevantFor` keep their own
owner/view-target/distance logic after the phase check.

An actor that stops being relevant would normally linger on the client for `RelevantTimeout`
(5 s). The subsystem closes the channel with `EChannelCloseReason::Relevancy` right away for:
- every connection an actor is no longer relevant to, when its keys change;
- every registered phased actor a viewer can no longer see, when its membership changes.

Dormant actors are never re-evaluated for relevancy by the driver, so phased actors must be
`DORM_Awake`; the subsystem calls `FlushNetDormancy` on key changes as a safety net.

PlayerStates are always relevant by engine design. The owning connection always sees its own pawn,
controller, PlayerState and owned actors.

### 3.1 Replication Graph (`ActorPhasingReplicationGraph` module)

Replication Graph replaces per-actor relevancy with graph nodes and never calls `IsNetRelevantFor`.
The adapter provides:

- `UActorPhasingReplicationGraphNode`: a global node holding one child per phase key. Actors are
  filed under every key they carry; a connection gathers the children of every key in its viewers'
  memberships (`UActorPhaseSubsystem::FindMembership` on the viewer's PlayerController). Children
  are `UReplicationGraphNode_GridSpatialization2D` grids by default (`bSpatializePhases`), so a big
  phase is still distance culled; `bAlwaysRelevant` actors and non-spatialized setups use actor
  lists. Empty phases release their children. Key changes arrive through
  `OnActorPhaseKeysChangedNative`; an actor that loses every key is parked in an "unphased" list
  (gathered for everyone) until the graph re-routes it. Create it with `CreateNewNode` and
  `AddGlobalGraphNode` so `PrepareForReplication` reaches the grids.
- `UActorPhasingReplicationGraph`: a drop-in driver with the engine's basic layout (grid,
  always-relevant list, per-connection always-relevant node, owner-only actors) plus the phase
  node. Routing order: owner-only → phased (`ShouldRouteToPhaseNode`: any effective key, including
  inherited ones) → always-relevant → grid. It re-routes actors that gain or lose a phase. Config
  keys (`[/Script/ActorPhasingReplicationGraph.ActorPhasingReplicationGraph]`): `GridCellSize`,
  `PhaseCellSize`, `bSpatializePhases`.

```ini
[/Script/OnlineSubsystemUtils.IpNetDriver]
ReplicationDriverClassName="/Script/ActorPhasingReplicationGraph.ActorPhasingReplicationGraph"
```

Own graph: create the node in `InitGlobalGraphNodes`, send actors with
`UActorPhasingReplicationGraph::ShouldRouteToPhaseNode(Actor)` to it in `RouteAdd/RemoveNetworkActorToNodes`,
and bind `OnActorPhaseKeysChangedNative` to re-route actors whose phased/unphased state flips
(the node re-files phased→phased changes itself).

Actors visible to observers are filed in a second set of per-key children. Members of a phase
gather both sets; a connection whose viewers only observe the phase gathers the observable set.
Observability changes re-file the actor through the same key-change event.

Notes: effective keys are resolved when an actor is added, so an owned or attached actor that
inherits its phase is re-filed only when its own keys change or when you call
`NotifyActorPhaseKeysChanged`. An actor with several keys is gathered once per matching key; the
graph's per-connection frame bookkeeping replicates it once. The plugin declares the engine's
Replication Graph plugin as a dependency, which is inert until a driver class is configured.

### 3.2 Iris (`ActorPhasingIris` module)

Iris decides relevancy with filters. The adapter uses **exclusion groups**: every distinct key set
becomes one group (`UReplicationSystem::CreateGroup` + `AddExclusionFilterGroup`), every phased actor
joins the group of its key set, and each connection's status on a group is `Allow` exactly when its
membership intersects the set. Observer visibility is part of the group identity: observable and
member-only actors with the same keys use separate groups, and an observable group is also allowed
for connections whose observed phases intersect it. Exclusion groups run before dynamic filters, so the spatial filter
still applies within a phase.

`UActorPhasingIrisSubsystem` (world subsystem) does the work: it stays idle until the world's net
driver reports Iris on a server, then seeds itself from the registry and, every tick, reconciles
connections (`GetConnectionHandle().GetParentConnectionId()` ↔ the connection's PlayerController
membership) and retries actors that began replicating after they were filed (`FlushPending`).
`DescribeState()` prints groups, actors and connections.

Limits: Iris caps groups (`MaxNetObjectGroupCount`, 2048 by default) and this adapter uses one per
distinct key set, released when empty; a creation failure keeps the actors pending (invisible) and
logs an error. Owned actors with keys disjoint from their owner's membership are filtered (the
pawn mirrors membership, so it is not affected). Group statuses are per parent connection; child
(split-screen) connections follow their parent. Iris is still experimental in 5.8 and needs the
usual project configuration (`net.Iris.UseIrisReplication 1`, filter definitions); the adapter
adds nothing to that setup.

## 4. Collision and interaction

`IgnoreActorWhenMoving` affects a component's own movement sweeps only. Inside a collision scope the
subsystem keeps pawn↔pawn and pawn↔actor ignore pairs in sync with the phases. Everything else
(overlap queries, traces, placement validation, targeting, interaction) is the consumer's
responsibility: filter hits with `AreActorsInCompatiblePhases` and gate server RPCs with
`CanViewerInteractWithActor`.

## 5. Lifecycle pattern for consumers

1. Player enters a volume → `ResolvePhaseForViewer` picks `{Domain, Id}` from the player's data.
2. If that phase has no actors yet, spawn them with `SetSinglePhase(Key)` and `SetCollisionScope(Volume)`,
   then `RegisterIdleRule(Key, Grace)`.
3. When the player changes selection while inside, update the data and call `ReapplyForPawn`.
4. `OnPhaseUnviewed(Key)` → flush persistence, destroy the phase's actors.
5. Player leaves → the volume resets membership to base; the idle rule starts counting.
6. Optional preview: while a player is near the volume, `AddObservedPhase(Key)` shows the phase's
   props without moving the player into it; `RemoveObservedPhase` when they walk away.

## 6. Diagnostics

- `ActorPhasing.Debug.Draw 1` draws volumes and a `Phase: ...` label above each viewed pawn
  (servers and standalone).
- `ActorPhasing.Debug.Log 1` logs every membership and key change (`LogActorPhasing`).
- `ActorPhasing.Dump` prints viewers, phases, scopes and idle rules.

## 7. Tests

Automation tests live under each module's `Private/Tests` folder: `ActorPhasing.Rules`,
`ActorPhasing.Membership`, `ActorPhasing.Volume` and `ActorPhasing.Subsystem` (core),
`ActorPhasing.ReplicationGraph` (node gathering, re-filing on key and membership changes, routing)
and `ActorPhasing.Iris` (key sets, group sharing, status updates, pending retries; run against a
recording backend, no replication system needed). They run in the editor's Session Frontend or
with `-ExecCmds="Automation RunTests ActorPhasing"`.

## 8. Roadmap

- Gameplay Debugger category.
- Demo map.
