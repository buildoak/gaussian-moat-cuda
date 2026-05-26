# Tile-Frontier Streaming Redesign

Status: partial implementation contract, diagnostic only, not proof evidence.
Date: 2026-05-25.

This note consolidates the sequential design, math, red-team,
implementation, and audit passes for the lower-bound source-propagation
streaming redesign.

The first diagnostic pieces now exist: `tileop_port_stream`, `stream_checkpoint`,
`source_tileop_port_stream_runner`, stream equivalence tests, and static-reach
equivalence gates. They are implementation/regression surfaces, not W-scale
claim machinery. The remaining target is still a true bounded streaming fold:
active microband plus live frontier, with proof/replay outside the hot path.

Implementation status: the current `source_tileop_port_stream_runner` is a
diagnostic MVP. It supports `GEO_I_PORT_DIAGNOSTIC` seeding with
`--seed-inner-flags` and resume-equivalence through its own checkpoint path. It
does not yet accept certified/incoming live source handoffs as a campaign
source, and `--death-out` is deliberately unsupported. Any death or
source/origin claim must still use the materialized diagnostics plus independent
proof gates, not this streaming runner alone.

## Objective

The current LB TileOp-port runner is useful as a diagnostic oracle, but it is
not the W-scale execution substrate. It enumerates active tiles for a band,
builds a full TileOp slab, converts that slab into a full port graph, and then
processes the graph. That is acceptable for K26 diagnostics and small
equivalence gates. It is not acceptable for composed runs that should eventually
handle widths around `100M` or high-radius probes around `R=400M`.

The target is a streaming source-survival engine:

- reuse the existing TileOp code as the local oracle;
- preserve exact source continuation across cuts;
- keep hot memory bounded by active microband plus live frontier;
- store only first-source artifact, previous/current live handoff, and active
  last-band summary in the hot campaign state;
- leave claim-grade terminal proof to independent replay or accumulator gates.

## Accepted Math State

The live state is the exact separator `H_i`, not "tiles reached" and not
"source ports reached":

```text
H_i = carry_atoms + component_partition + source_bit_per_component
```

`carry_atoms` must include every frontier atom that can affect future
connectivity, source and neutral. `component_partition` is the exact
equivalence relation induced by the processed prefix over those atoms.
`source_bit_per_component` marks which partition classes are source-connected.

This is the minimum accepted hot state:

```text
LiveHandoffV1 {
  schema/version
  k_sq
  cut_radius
  carry_width
  band_or_schedule_index
  source_mode
  source_id
  geometry_id
  first_source_artifact_hash
  build_id
  oracle_identity
  schedule_digest
  carry_window_predicate_id
  port_overhang_policy_id
  overflow_summary
  separator {
    carry_atoms[] { atom_id, norm_sq }
    component_partition[][]
    source_bit_per_component[]
  }
}
```

Current implementation status: the checked-in `LiveHandoffV1` is narrower than
this target envelope. It carries `k_sq`, `cut_radius`, `carry_width`,
`source_mode`, `source_id`, `geometry_id`, `build_id`, schedule digest fields,
`overflow_summary`, and the live separator. `band_or_schedule_index`,
`first_source_artifact_hash`, explicit oracle identity, carry-window predicate
id, and port-overhang policy id are target fields until implementation and
tests bind them.

Hot state must not contain historical component inventories, dense union-find
roots, transient local TileOp labels, full TileOp slabs, whole-band port graphs,
or geometric `geo_I` / `geo_O` summaries as source facts.

## Stable Atom Ids

Tile IDs alone are not enough. A tile may contain several unrelated local
components. Persisting only the tile identity can weld components that were not
connected.

Local TileOp labels are not enough. They are local grouping data inside one
TileOp emission and are not stable persisted identities.

The stable handles are canonical atom ids:

- coordinate Gaussian-prime atom ids, when the frontier is coordinate based;
- canonical TileOp port atom ids, when the frontier is port based.

For a port atom, the identity is logically:

```text
grid/oracle/wire identity + tile coordinate/origin + face + ordinal
```

The implementation already exposes `port_atom_id(...)` and
`decode_port_atom_id(...)`. Streaming code must preserve the exact support,
carry-window, and port-overhang semantics tied to those ids.

## Required Transition

For each segment or microband:

1. Validate incoming `LiveHandoffV1` against `k_sq`, cut, carry width,
   source identity, schedule, build/oracle identity, and overflow summary.
2. If this is the first band, seed source only from an explicit source mode
   such as origin/certified/wired source. Later bands seed only from incoming
   `source_bit_per_component`.
3. Reject any fresh `geo_I`, `certified_source`, or `--seed-inner-flags` when
   an incoming live handoff exists.
4. Produce TileOps in deterministic tile order.
5. Decode each TileOp into canonical port atoms. Use local TileOp labels only
   to union ports inside that tile.
6. Stitch neighbor seams by stable face/ordinal ordering.
7. Add incoming separator partition and source bits exactly once.
8. Add accepted coordinate-to-port bridge edges where applicable.
9. Close connectivity.
10. Compact to the outgoing carry window while preserving all source and
    neutral carry classes.
11. Emit the outgoing canonical `LiveHandoffV1`.
12. If no source-connected carry remains, emit diagnostic death only as:
    `previous_live_handoff + active_last_band_summary`.

The progress row alone is not terminal evidence.

## Why Neutral Carry Is Non-Negotiable

Two shortcuts are invalid:

- Carrying only source-marked atoms gives false death. A neutral frontier class
  can merge into source in a later band and then carry source farther.
- Treating all carry atoms as source gives false continuation. A neutral class
  can reach the next cut without ever merging into source.

Therefore both neutral classes and source bits must survive every cut.

## Claim Boundary

Streaming source-survival output is diagnostic until proof gates promote it.

`SOURCE_TERMINAL` means the live streaming state observed no source-connected
carry at the cut under the implemented diagnostic rules. It is not a
`SOURCE_DEAD_CERT`.

`SOURCE_DEAD_CERT` additionally requires independent proof gates:

- zero overflow under accepted oracle/build identity;
- exact BZ/schedule binding where relevant;
- negative final guard;
- positive coordinate Gaussian-prime source path to the claimed endpoint;
- coordinate-to-port bridge safety, checked after closure or for all incoming
  coordinate carry;
- terminal inventory through independent replay or claim-grade accumulator;
- independent checker acceptance, not producer booleans.

Port overhang can keep continuation alive, but a port support norm is not a
coordinate endpoint.

## Implementation Architecture

Keep ownership split clean:

- `source_propagation.{h,cpp}` owns separator semantics, live handoff
  validation, canonicalization, no-fresh-source rules, and last-band replay.
- `tileop_port_graph.{h,cpp}` remains the materialized comparator and small
  oracle.
- New `tileop_port_stream.{h,cpp}` owns per-tile decoding, bounded seam state,
  streaming fold telemetry, and later true streaming DSU.
- New `stream_checkpoint.{h,cpp}` may own restart envelopes around
  `LiveHandoffV1`.
- New `source_tileop_port_stream_runner.cpp` should be a separate runner, not
  an overloaded mode in the current diagnostic runner.

The current materialized runner remains useful because it is battle tested and
already exercises the source-propagation sidecar. It should be used as the
small-scale equivalence oracle, not as the W-scale engine.

## Implementation Phases

### Phase 0: Baseline And Invariant Hardening

Before adding streaming behavior, run and preserve existing local gates.

Add tests that reject:

- fresh source when incoming live handoff exists;
- transient roots, dense ids, and local TileOp labels as live atom ids;
- malformed live separators;
- last-band summaries with mismatched envelope fields.

Acceptance:

- existing sidecar and verification tests pass;
- v1/live manifest behavior remains byte-compatible where expected;
- all new outputs are explicitly diagnostic, not claim-grade.

### Phase 1: Per-Tile Port Decoder Parity

Factor reusable per-tile canonical port decoding from the materialized graph
path into additive `tileop_port_stream.{h,cpp}` APIs.

Acceptance:

- decoder output matches `make_tileop_port_band` on current fixtures;
- adversarial fixtures cover duplicate tile, port-count mismatch, overflow,
  empty tile, face/ordinal stability, and local-label non-persistence;
- no true streaming DSU or W-scale runner is added yet.

### Phase 2: Linear Microband Fold

Build a first stream runner that folds bounded microbands and may still use
`process_band_live` as a correctness backend for each microband.

Acceptance:

- one full materialized band and equivalent microband schedule produce
  byte-identical live handoffs;
- uninterrupted and chunked/resumed runs produce byte-identical checkpoints;
- comparison checks separator bytes and fields, not only live/dead booleans.

### Phase 3: Checkpoint Envelope

Wrap `LiveHandoffV1` in a stream checkpoint envelope with schedule index,
artifact hashes, source identity, build/oracle identity, and overflow summary.

Acceptance:

- mismatched `k_sq`, cut, schedule, build, source, or carry width rejects;
- restart from the previous saved live handoff reproduces the uninterrupted
  next handoff;
- checkpoint size tracks frontier, not processed history.

### Phase 4: True Streaming DSU And Seam Fold

Replace microband `BandInput` materialization with bounded streaming state:
per-tile decode, ephemeral local unions, bounded seam maps, and frontier
compaction.

Acceptance:

- same handoffs as Phase 2 on all fixtures and probes;
- telemetry reports max resident tiles, TileOps, ports, edges, DSU nodes,
  frontier atoms, checkpoint bytes, and RSS;
- telemetry demonstrates scaling with active microband plus frontier, not total
  processed tile count.

### Phase 5: Streaming Last-Band Summary

Produce the active last-band transfer/death summary from the streaming fold.

Acceptance:

- `apply_last_band_summary(previous_live_handoff, active_summary)` replays the
  death decision;
- hostile neutral-weld and port-overhang fixtures pass;
- progress row alone cannot be accepted as death evidence.

### Phase 6: Independent Accumulator And Verifier

Add an independent checker for proof/event streams before any terminal result
can be called claim-grade.

Acceptance:

- verifier checks complete stream observation, canonical order,
  duplicate-free inventory, finalized retired component, max coordinate
  summaries, overflow, and terminal root;
- forged producer booleans are rejected;
- bridge safety counters are checked after closure or for all incoming
  coordinate carry.

### Phase 7: Remote Smoke, Then Scale Probes

Only after local gates pass, run small remote GPU/CPU smokes.

Acceptance:

- zero overflow;
- exact restart reproducibility;
- bounded memory telemetry;
- no CUDA claim unless CUDA kernels actually fed the LB path.

## W-Scale Feasibility Gates

For `R=60M`, `R=400M`, and eventual wide composed runs, feasibility is not
proven by a final live/dead flag. It is proven by scaling telemetry.

Required measurements:

- exact tile count and id-range preflight;
- sampled TileOp pressure at target radii;
- zero TileOp overflow;
- per-tile and per-port throughput;
- max resident TileOps/ports/edges/DSU nodes;
- live frontier size and checkpoint size per cut;
- RSS and allocator high-water marks;
- materialized-vs-streaming handoff equivalence on thin probes;
- uninterrupted-vs-resumed byte equivalence;
- full-width projection based on observed bounded-memory behavior.

Thin `R=400M` probes are feasibility signals. They do not prove `W=100M`.
The W-scale proof is the combination of bounded resident state plus throughput
projection under a schedule that never materializes the full width.

## Rejected Shortcuts

- Persisting tile ids as the live frontier.
- Persisting local TileOp labels.
- Dropping neutral carry.
- Marking all carry as source.
- Reseeding from `geo_I` after the first/source band.
- Treating static-annulus `geo_I` / `geo_O` evidence as source evidence.
- Treating a progress row as terminal proof.
- Treating port support as coordinate endpoint evidence.
- Accepting accumulator summary booleans without independent replay/checking.
- Running expensive Vast campaigns before local equivalence, resume, and
  bounded-memory gates pass.

## First Coding Goal

Use this as the next implementation goal:

```text
Implement the pre-streaming invariant and decoder-parity slice for LB
tile-frontier streaming. Add direct live-separator stable-atom validation and
last-band envelope mismatch tests, then factor the per-tile canonical port
decoder used by make_tileop_port_band into additive tileop_port_stream.{h,cpp}
APIs. Do not add true streaming DSU or a W-scale runner yet.

Done means v1/live manifest behavior stays byte-compatible, new decoder output
matches make_tileop_port_band on current and adversarial fixtures, no fresh
source is accepted after handoff, transient atom ids are rejected, local
sidecar/verification CTests pass, wide-vs-stitched live handoff bytes match,
and all outputs remain DIAGNOSTIC_NON_CLAIM.
```
