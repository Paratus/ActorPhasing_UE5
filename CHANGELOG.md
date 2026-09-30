# Changelog

## 1.0.0

- Initial release for Unreal Engine 5.8.
- Phase keys, actor phase component, viewer membership component (owner-only replicated).
- Relevancy rule library for `IsNetRelevantFor`, interaction/targeting gate, owner and attach
  inheritance.
- World subsystem: registries, immediate actor-channel closing on phase switches, collision
  scopes (move-ignore), idle-phase notifications, debug console variables and dump command.
- Phase volume actor with per-viewer resolution (native event, resolver object or fixed phase),
  spawn-inside retry and collision scope maintenance.
- `APhasedActor` convenience base, project settings, automation tests, documentation.
- Optional `ActorPhasingReplicationGraph` module: a per-phase gathering node and a drop-in
  replication graph driver.
- Optional `ActorPhasingIris` module: exclusion-group filtering that follows memberships under
  Iris, activated automatically on Iris servers.
- Native change events on the world subsystem for replication adapters.
- Observed phases: a viewer can watch a phase's world content (props, scenery) without joining
  it, e.g. to preview a yard from a distance. Observation never reaches the pawn, grants no
  interaction, keeps idle rules armed, and is honored by the standard driver, the Replication
  Graph node and the Iris adapter. Per-actor opt-out with `bVisibleToObservers`.
