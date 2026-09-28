# 2. Unified runtime bus, ECS and jobs

## Runtime integration diagram

```text
                   Android MotionEvent → TouchBegan / TouchMoved / TouchEnded
                   ThermalManager      → ThermalEvent → QualityChanged
                                              │
  Gameplay script ← native host hooks ←────── EventBus ─────→ UI/HUD listener
         │                                    ↑  │
         ├→ ECS World → EntitySpawned/Destroyed │→ Physics2D: CollisionEvent (planned bridge)
         ├→ SaveGameEvent (future)               │→ Audio/Net/AI (future modules)
         └→ PrismScript println → offline Log    │
                              TickEvent / FixedTickEvent ← Engine Clock
```

`EventBus::subscribe<T>()` returns a listener ID. `publish<T>()` copies the handler list under a lock then calls handlers outside the lock (callbacks can subscribe/unsubscribe). Events are synchronous and typed. `ServiceRegistry` holds named shared services. Module lifecycle: sorted `on_boot` → `on_start` → repeated fixed ticks, variable tick, render → reverse `on_shutdown`. The bus exists and is tested. **Cross-module adapters for most future modules do not yet exist.** The Android host publishes touch events; the physics demo currently accesses its local world directly and does not publish collision events. Bridge this before claiming universal integration.

## ECS design

`EntityId{index,generation}`; destroy increments generation and recycles the slot, invalidating stale IDs. `ComponentPool<T>` is a sparse-to-dense index with contiguous `std::vector<T>` and swap-remove. `World::each<Ts...>` scans the first pool and joins other pools; add a smallest-pool query planner later. `Transform` is present on entity creation. Fixed-order `System` / `FnSystem` executes per tick. Transform snapshot hash supports replay comparison; it does not yet serialize a restorable rollback state.

## Job system design

`JobSystem` holds a bounded mobile worker count (hardware threads minus one, capped at 8); `schedule` returns an atomic-counter `JobHandle`; `parallel_for` chunks work; `wait` assists queued work. Never mutate the same ECS component array from two jobs without partitioning or synchronization. `schedule_after` is a blocking dependency prototype; a production DAG should replace it to avoid worker starvation. There is no actual cluster affinity today.

## 2D solver

Fixed `1/60` s step, capped catch-up. Semi-implicit Euler, broadphase AABB pairs, SAT for rotated box/box; circle/circle and box/circle; impulse/friction contact resolution; sleeping and positional correction. Determinism depends on floating-point/ABI/compiler and is not guaranteed across devices. Joint descriptors, polygons, capsules, and continuous collision are API reservations, not implementations. Do not rely on them in shipped games yet.
