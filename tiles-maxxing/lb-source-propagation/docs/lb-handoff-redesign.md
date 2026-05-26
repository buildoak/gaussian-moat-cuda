# LB Handoff Redesign

Status: accepted design and partial implementation contract, not proof evidence.
Date: 2026-05-24.

## Objective

The Phase 1 sidecar proved the right source-propagation shape, but the current
handoff carries explicit historical component inventories. That is acceptable
for fixtures and small diagnostics, but not for K26 and not for future very wide
lower-bound sweeps.

The redesign target is:

- live continuation state bounded by the frontier, not by historical component
  size;
- exact source/origin reachability across stacked bands;
- no reinterpretation of static `geo_I` / `geo_O` evidence as source evidence;
- proof inventory handled by an independently checkable accumulator or replay
  ledger;
- TileOp remains the local connectivity oracle.

## Current Problem

The current `SeparatorState` mixes two concerns:

```text
carry_atoms
component_partition
source_bit_per_component
component_inventory
```

The first three fields are the live separator. They are sufficient for future
connectivity. `component_inventory` is certificate/debug payload. Today it is
stored as explicit atom-id vectors, merged forward on every band, and written in
`LB_SOURCE_CARRY_MANIFEST_V1`.

That makes the hot handoff grow with the historical component:

```text
live continuation needed: O(frontier)
current inventory payload: O(component history)
```

The 2026-05-23 K26 probe already showed this direction: after only five
continuation bands the source inventory count was `2,628,628`, the continuation
manifest was about `38 MB`, and the prefix witness was about `127 MB`. K26's
expected source component size is `14,542,615,005`, so a listed terminal
inventory is not a viable execution substrate.

## Accepted Model

### LiveHandoffV1

The hot handoff is exact frontier state only:

```text
LiveHandoffV1 {
  schema
  k_sq
  source_identity
  band_index
  cut_radius
  carry_width = ceil_sqrt(k_sq)
  carry_window_predicate
  oracle_identity
  overflow_summary
  carry_atoms[]
  component_partition[]
  source_bit_per_component[]
}
```

`carry_atoms` contains all stable frontier atoms that can affect future
connectivity, source and neutral. Atom ids must be stable coordinate atom ids or
canonical TileOp port atom ids. They must not be transient union-find roots,
TileOp group labels, dense component ids, or historical inventory ids.

`component_partition` is the equivalence relation over exactly those carry
atoms induced by the processed prefix. `source_bit_per_component` attaches to
partition classes, not individual atoms.

The hot handoff must not contain `component_inventory`, historical member
lists, summary source counts, or `geo_I` / `geo_O` continuation summaries.

### Transition Rule

For each band:

1. First-band source is seeded only from `ORIGIN_SOURCE`, `CERTIFIED_SEED`, or
   explicitly checked `WIRED_SOURCE`.
2. Later bands seed only from incoming `LiveHandoffV1` source bits.
3. The active graph is incoming carry atoms plus local oracle atoms/edges plus
   accepted bridge edges.
4. Closure unions incoming partition classes, local oracle edges, and bridge
   edges.
5. Source bits propagate by OR across unions.
6. The outgoing handoff keeps every closed atom in the new carry window, source
   or neutral.
7. Proof/inventory state is updated before frontier compaction, but it does not
   seed future bands.

This is the minimal live state needed for exact continuation. Dropping neutral
carry atoms can lose future source paths. Treating all carry atoms as source can
invent source paths. Dropping the partition loses prefix connectivity.

## Proof Ledger

Proof inventory moves out of the hot handoff.

`ProofLedgerV1` is a separate replayable evidence stream. It may be large and
chunked. It does not seed future bands.

Required event classes:

```text
ingest_atom
local_union
handoff_union
bridge_union
retire_from_frontier
merge_classes
finalize_component
```

The ledger or accumulator must track all live carry classes, including neutral
classes. This is non-negotiable: a neutral component may later merge into the
source component, and then its prior inventory becomes part of terminal source
inventory.

### Accumulator Domain

Claim inventory is over coordinate Gaussian-prime atoms. TileOp port atoms are
connectivity witnesses, not inventory atoms.

A claim-grade accumulator must bind at least:

```text
count
digest/root
max_norm_sq
max_norm_atom_ids
complete_stream_observed
canonical_order or verifier-checked canonical set union
duplicate_free
retired_component_finalized
overflow_checked
terminal_death_root
```

The current `claim_grade_accumulator` checker is not enough: it accepts summary
fields and producer-supplied booleans. A real claim-grade path needs an
independent checker that replays or verifies the accumulator stream.

## Terminal Death

`SOURCE_TERMINAL` is diagnostic state. It is reached after full guarded closure
when a source-connected component existed but no source-connected class remains
in the final carry window:

```text
[R - ceil_sqrt(K), R]
```

For TileOp-port continuation, the port overhang rule remains load-bearing:
canonical port atoms whose stable support overlaps the next band can keep the
source alive. Final negative guard proof must account for this, not only
coordinate atoms.

`SOURCE_DEAD_CERT` additionally requires:

- zero claim overflow;
- accepted oracle/build/profile identity;
- negative guard at the terminal radius;
- positive coordinate Gaussian-prime source path to the claimed endpoint;
- claim-grade BZ/schedule binding where applicable;
- complete terminal inventory summary checked by independent replay or a
  claim-grade accumulator checker.

## Coordinate-to-Port Bridge

Coordinate-to-port bridge evidence remains separate from the live handoff.

For claim-grade use, every source-relevant incoming coordinate carry atom must
either:

- bridge into the TileOp-port graph;
- have no legal next-band Gaussian-prime candidate within squared distance
  `K`;
- have only dead-end candidates whose components have no encoded face ports.

The auditor pass found a subtle risk: checking only atoms that are source at the
incoming seam can be too early, because neutral carry classes can merge into
source inside the first TileOp band. Therefore claim-grade bridge safety must
either check all incoming coordinate carry atoms, or evaluate unsafe candidates
after closure under first-band merges.

Mixed coordinate/port atom chains remain non-claim until expanded into checked
coordinate Gaussian-prime paths.

## Rejected Alternatives

- Carrying only source atoms.
- Treating all carry atoms as source.
- Pure `geo_I` / `geo_O` continuation summaries.
- Keeping historical `component_inventory` in the hot handoff.
- Tracking only currently source inventory in the accumulator.
- Treating TileOp port atoms as terminal coordinate inventory.
- Treating content-addressed TileOp slabs as proof by themselves.
- Accepting producer-emitted accumulator flags as claim-grade proof.

## Runtime Shape

Use microbands as the correctness unit and chunks as the execution unit.

TileOp slabs may be cached for debugging or operational reuse, but they should
not become the proof substrate. Raw TileOp storage can explode at very wide
scales. If caching is used, cache keys must bind `K`, exact radii, schedule/BZ
digest, tile order, TileOp wire version, build/commit, producer config, and
artifact hashes.

The current `source_tileop_port_runner` still materializes full bands and full
port graphs. That is acceptable for Phase 1 diagnostics, but not for W-scale
runs. Streaming TileOp consumption should come after the data model split and
independent accumulator checker exist.

## Two Execution Modes

The W-scale design should expose two related but different modes. Keeping them
separate prevents the simple lower-bound sweep from inheriting the full
certificate burden too early.

### Mode 1: Seed-Death Sweep

This mode tracks whether a chosen source seed or origin component survives
through a wide annulus. It is the first operational target for W around
`100M`, with execution chunked into coarse bands such as `50k`.

The required state is the live frontier plus source bits. The run does not
retain exact historical atom inventory in the hot path. It needs exact
continuation semantics at each cut and a reliable death bracket:

```text
source live at cut R_i
source dead by cut R_{i+1}
```

For this mode, `geo_I` and `geo_O` are alignment surfaces, not source
certificates. A static-annulus `geo_I` touch is not enough to seed source.
However, once a certified source/frontier handoff enters a wide shell, the
geometric inner and outer surfaces still need to be aligned with the handoff
cuts so that the shell being tested is exactly the shell whose source survival
is reported.

Protocol:

1. Inputs are `k_sq`, global shell `[R0, R1]`, nominal band width, oracle/build
   identity, schedule/BZ digest, and a certified source input: `ORIGIN_SOURCE`,
   `CERTIFIED_SEED`, `WIRED_SOURCE`, or incoming `LiveHandoffV1`.
2. The schedule is a strict increasing sequence of cut radii. Every segment
   must have width at least `ceil_sqrt(k_sq)`. The final cut is exactly `R1`;
   repaired/aligned cuts are recorded with a schedule digest.
3. Each band consumes incoming live carry, local TileOp port connectivity,
   accepted bridge edges, and local oracle atoms. It unions incoming partition
   classes, local edges, seam edges, and bridges. Source bits propagate by OR.
4. The outgoing handoff keeps all carry atoms in the final carry window, source
   and neutral. Neutral carry is load-bearing because it may merge into source
   in a later band.
5. Once an incoming handoff exists, later bands must not seed from `geo_I`,
   `certified_source` flags, or fresh first-band source rules. Apparent source
   revival after death is a run-semantics failure, not a mathematical event.
6. At every accepted live cut, persist or content-address the exact
   `LiveHandoffV1` plus envelope. Mode 2 needs the last-live handoff, not just
   the first-dead progress row.

Claim status: diagnostic lower-bound/source-survival evidence unless paired
with a verifier-accepted source seed, bridge proof, negative guard, and
terminal proof artifact.

### Mode 2: Furthest-Component Refinement

This mode asks for the furthest extent of the source component or the last
source-connected component before death. It should not force every coarse band
to retain full historical inventory or full paths.

The intended strategy is:

1. Run Mode 1 with coarse, restartable bands.
2. Identify the last live cut `R_L` and first clean dead cut `R_D`.
3. Restart from the exact saved `LiveHandoffV1` at `R_L`.
4. Re-run only `[R_L, R_D]` with narrower cuts and richer proof
   instrumentation.
5. Repeat refinement until the death boundary, target witness, or furthest
   component statistic is resolved at the desired precision.

This is the key refinement idea: the wide sweep finds the small region that
matters; expensive precise evidence is collected only around the last-live /
first-dead transition. The rest of the W-scale scan remains frontier-only.

A dead progress row alone is not enough. A rich last-band reachability summary
can be enough. The difference is whether the band artifact preserves the
boundary transfer needed to apply the exact incoming source frontier locally.
If the last-live handoff or an equivalent boundary summary is unavailable, the
refinement must restart from an earlier saved checkpoint or from the certified
seed.

The refinement pass may enable listed inventory for tiny windows, replayed
coordinate paths, extra bridge expansion, or claim-grade accumulator emission.
Those are local refinements, not requirements for the entire W-scale run.

For lower-bound claims, separate coordinate facts from port-continuation facts:

- coordinate `max_source_norm_sq` and coordinate tie atoms are claim-relevant;
- TileOp port/support norms are diagnostic continuation evidence;
- TileOp port atoms must be expanded into checked coordinate Gaussian-prime
  paths before they can support a coordinate furthest-component claim.

### Last-Band Reachability Summary

The elegant last-band object is not a historical inventory. It is a local
transfer summary for the band that died, plus the incoming live frontier at its
inner cut.

Conceptually:

```text
LastBandReachabilitySummaryV1 {
  schema
  k_sq
  source_identity
  band_index
  r_start
  r_outer
  carry_width
  incoming_interface
  carry_window_predicate
  outer_continuation_predicate
  schedule_digest
  oracle_identity
  bridge_policy
  overflow_status

  inner_boundary_atoms_or_ports[]
  outer_boundary_atoms_or_ports[]
  local_boundary_partition[]
  local_component_max_coordinate_norm[]
  local_component_max_coordinate_atom_ids[]
  coordinate_bridge_records[]
  local_witness_refs[]
  atom_set_digest
  partition_digest
}
```

`r_start` and `r_outer` bind the local annulus and play the local `geo_I` /
`geo_O` role for guard predicates. The incoming source is not inferred from
the inner surface; it is applied through the incoming `LiveHandoffV1` source
bits. The band summary says which inner boundary/port atoms connect to which
outer boundary/port atoms and what the furthest coordinate Gaussian-prime atom
is inside each boundary-connected local component.

The local refinement algorithm is:

1. Load the incoming `LiveHandoffV1` at `r_start`.
2. Union its full frontier partition with the band's local boundary partition.
3. Mark roots source when they contain an incoming source-bit class.
4. Death is confirmed if no source root touches outer carry / `geo_o_cut`,
   respecting TileOp port overhang.
5. The last-band furthest candidate is the maximum coordinate atom summary
   among source roots.

This is the version of "inspect the last band" that is actually exact. It is
still O(frontier plus boundary-connected component summaries), not O(history).

Two caveats are load-bearing:

- Storing only the source-carrying ports is not enough in general. Neutral
  incoming frontier classes must also be present, because a neutral class can
  weld to source inside the last band and then carry additional local reach.
- The summary can identify the coordinate furthest candidate diagnostically.
  A claim-grade endpoint still needs targeted path expansion or replay for the
  selected source-to-coordinate path, including any prefix equivalence used by
  neutral carry classes.

### Minimal Campaign State

For the normal W-scale seed-death campaign, the runner does not need to keep
all band artifacts. The minimal correctness state is first-plus-rolling-last:

```text
CampaignState {
  first_source_artifact
  current_live_handoff
  previous_live_handoff
  active_band_summary
  best_terminal_summary_if_dead
}
```

`first_source_artifact` binds the certified source identity and the global
`geo_I` / `geo_O` shell. It may be an origin-prefix certificate, a certified
seed, or a trusted incoming handoff. It is not copied into every band.

`current_live_handoff` is the only state needed to continue into the next band.
`previous_live_handoff` is retained while processing the active band so that, if
the active band kills the source, Mode 2 has the exact incoming source frontier
needed to inspect/refine that last band.

`active_band_summary` is overwritten band by band. If the source dies, the
runner persists the pair:

```text
previous_live_handoff + active_band_summary
```

That pair is the local final artifact for diagnostic furthest-component
refinement. Earlier band summaries are not required for the hot path.

This keeps the campaign memory bounded by:

```text
O(first source artifact + two live frontiers + one active band summary)
```

The tradeoff is resumability. If only first-plus-last is persisted and the
process dies mid-sweep, recovery may require replay from the first source
artifact. For practical long runs, the runner may keep a single rolling
checkpoint or sparse checkpoints, but those are operational restart artifacts,
not mathematical continuation state.

If restart convenience is not required, no sparse checkpoint is required. The
last-band artifact is still sufficient for diagnostic death/furthest
localization, because it carries the previous live frontier and the local
transfer summary for the band that killed source.

### First Implementation Slice

Keep the first implementation additive. Do not rewrite the TileOp core and do
not break `LB_SOURCE_CARRY_MANIFEST_V1`.

Add these live types beside the existing `SeparatorState`:

```text
LiveSeparator {
  carry_atoms
  component_partition
  source_bit_per_component
}

LiveHandoffV1 {
  envelope
  LiveSeparator
}

LiveProcessResult {
  reject
  diagnostic
  carry_width
  outgoing LiveSeparator
  terminal_source_dead
}
```

Add adapters:

```text
live_separator_from_separator
separator_from_live_separator
canonicalize_live_separator
validate_live_separator
```

The adapter from live to legacy `SeparatorState` leaves
`component_inventory` empty. That is reachability-correct for continuation,
but it is not claim-grade terminal inventory.

Then factor the current `process_band` closure into two public fronts:

- `process_band(...)`: existing listed-inventory/debug behavior, byte-compatible
  with current tests and v1 manifests;
- `process_band_live(...)`: same closure and carry semantics, but no historical
  inventory growth and no terminal listed inventory.

`process_band_live` and continuation runners must reject fresh source seeding
when an incoming handoff exists, unless a clearly marked diagnostic mode opts
into non-continuation behavior.

### Runner Strategy

Use the current `source_tileop_port_runner` as the materialized correctness and
parity gate. It can build the first live-summary artifacts while still using
the battle-proven TileOp-port conversion path.

W-scale execution should be a new streaming runner rather than an overloaded
mode in the diagnostic runner:

```text
source_tileop_port_stream_runner
```

The streaming runner state machine is:

```text
INIT
FIRST_LIVE_GATE
STREAM_SEGMENT
PORT_FOLD
HANDOFF
ROLL_SUMMARY
DEATH_OR_CONTINUE
```

It emits:

```text
run.progress.jsonl
run.profile.json
run.live-handoff.txt        optional rolling live checkpoint
run.death.json              previous_live_handoff + active_band_summary
run.abort.json              reject/exception state
run.sha256                  artifact ledger
```

`run.death.json` is diagnostic non-claim unless later independent proof gates
promote it. It must bind `k_sq`, cuts, schedule digest, oracle identity,
overflow status, previous handoff hash, active summary hash, coordinate/port
metric split, and bridge safety counters.

### Optional Transfer-Summary Layer

After the live-handoff split exists, there is a higher-leverage representation:
each microband can be treated as a source-free transfer summary, an equivalence
relation over inner-frontier and outer-frontier atoms induced by local TileOp
connectivity. Source bits are then applied when summaries are composed.

This can make microbands cacheable and reusable across source queries, and it
is a natural future path toward segment-tree composition. It is not the first
implementation step. The first step remains a linear `LiveHandoffV1` path that
matches current `process_band` semantics and can be checked against one-wide
versus stitched runs.

## W-Scale Feasibility

For width `W=100M` and nominal band width `50k`, the sweep has about `2000`
bands. The target hot memory shape is:

```text
O(active TileOp streaming window + live frontier)
```

not:

```text
O(full band graph + historical source component)
```

The current code is not yet that substrate. `source_tileop_port_runner`
materializes full TileOp bands, builds full port graphs, and calls
`process_band`; `process_band` still merges explicit component inventories.
The live-handoff redesign removes the historical component term, but W-scale
also needs streaming TileOp/port consumption before very wide runs are
operationally sane.

Approximate active tile count for one band near radius `R`, tile side `S=256`,
and radial width `Delta`:

```text
tiles ~= pi * R * Delta / (4 * S^2)
```

At `R=100M`, `Delta=50k`, this is about `60M` tiles, roughly `15 GB` of raw
`TileOp` data at `256 B` each. At `R=1B`, the same band is about `600M` tiles,
roughly `153 GB` raw. Raw TileOp slab storage is therefore a debug/cache tool,
not a W-scale proof substrate.

Operational go/no-go:

- current materialized CPU TileOp-port runner: no-go for W-scale;
- Mode 1: conditional go after live-frontier hot state and streaming
  TileOp/port consumption exist;
- Mode 2: go only after Mode 1 brackets a clean live/dead transition;
- `50k` is a good restart/refinement granularity, not by itself a performance
  solution.

## Implementation Roadmap

1. Retire hot-path `LB_SOURCE_CARRY_MANIFEST_V1` parsing from LB runners.
   Keep v1 fixtures as historical compatibility coverage outside the live
   continuation protocol.

2. Introduce a live-frontier type.
   Split `LiveSeparator` from proof inventory. Live continuation fixtures must
   pass through live handoff files, not legacy carry manifests.

3. Add an envelope layer.
   Bind schema, `k_sq`, cut radius, carry width, producer kind, source mode,
   lineage mode, BZ status, artifact hashes, and version.

4. Add live handoff serialization.
   `LiveHandoffV1` carries envelope plus `LiveSeparator`. Runners read and
   write it through `--live-manifest-in/out`.

5. Add independent accumulator checker.
   It must consume accumulator/replay artifacts and verify count, digest/root,
   max norm, tie set, duplicate-freedom, complete stream, finalization, and
   overflow status. `source_dead_cert_check` must not trust self-declared
   claim-grade accumulator booleans.

6. Add listed-vs-accumulator fixtures.
   Include valid equivalence and hostile cases: missing atom, duplicate atom,
   wrong digest, wrong max tie, wrong order, incomplete stream, unfinalized
   retired component, and overflow mismatch.

7. Harden bridge safety.
   Add fixtures where neutral carry later merges into source and where unsafe
   bridge candidates are detected after closure or across all incoming carry.

8. Add domain preflight guards.
   Check coordinate atom id limits, port atom id tile-coordinate limits,
   schedule width, cut-radius monotonicity, stale handoffs, and partial chunk
   artifacts before long runs.

9. Add source-seeding policy guards.
   Later bands with incoming handoff must reject fresh `geo_I` /
   `certified_source` reseeding unless an explicit diagnostic mode says the run
   is non-continuation. A source revival after death must fail verification.

10. Split coordinate and port metrics.
   Report coordinate furthest facts separately from TileOp port/support
   diagnostics. Do not let port support norms become coordinate lower-bound
   evidence.

11. Add streaming accumulator production.
   Producer emits accumulator/replay leaves without requiring listed inventory
   in memory. This is still non-claim until checked independently.

12. Add streaming TileOp/port consumer.
   Replace full-band `BandInput` and set/map-heavy port graph materialization
   with bounded-memory microband consumption.

13. Prototype transfer summaries.
   Build source-free microband summaries only after linear live-frontier
   semantics are byte-stable and one-wide versus stitched equivalence is green.

## Verification Gates

- Current v1 tests stay green after parser centralization.
- One-big-band versus stitched-band equivalence compares canonical live
  frontier, partition, and source bits.
- Fixtures cover false weld, source-only carry loss, neutral merge into source,
  terminal death, K32 carry width, overflow rejection, and port overhang.
- Manifest v1 and v2 roundtrip tests pass.
- Accumulator checker accepts listed-equivalent fixtures and rejects all hostile
  fixtures.
- Uninterrupted versus chunked/resumed runs produce identical live handoff and
  accumulator roots on small and K26-scale probes.
- Synthetic long-chain probes show memory bounded by frontier plus active
  microband, not by history.
- Mode 1 retains exact `previous_live_handoff + active_band_summary` while
  processing the active band; sparse checkpoints are optional restart artifacts,
  not acceptance requirements.
- Refinement from the last-live handoff reproduces the original first-dead
  bracket before enabling richer instrumentation.
- Shared-cut coarse/fine equivalence is checked before accepting a refined
  death boundary.
- `previous_live_handoff + active_band_summary` reproduces the same diagnostic
  terminal state and coordinate furthest candidate as all-band replay on
  hostile fixtures.
- A progress row without previous handoff plus summary is rejected as terminal
  evidence.
- Later-band reseeding and source revival fixtures are rejected.
- Coordinate furthest facts and port/support diagnostics are checked as
  different fields.
- Target-radius `50k` streaming probes meet explicit time/RSS/storage gates
  before W-scale scheduling.
- `SOURCE_DEAD_CERT` remains rejected unless independent path, bridge, BZ,
  negative guard, and terminal inventory checks all pass.

## Stop Rules

Stop before claim-grade promotion if:

- live handoff roundtrips break;
- composed-vs-big live frontier parity changes;
- source lineage depends on transient component ids;
- neutral-to-source inventory lineage is missing;
- any source-relevant bridge candidate is unsafe after closure;
- accumulator and listed fixtures disagree;
- BZ remains schedule-only for a claimed row;
- memory still scales with historical inventory after the hot-handoff split.
