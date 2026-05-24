# Source Propagation Band Stitching

This note defines the Phase 1 lower-bound source-propagation protocol. The
sidecar does not change the existing TileOp layout, CUDA kernels, current
campaign CLIs, current compositors, or current static-annulus verdicts. It uses
the existing TileOp and band machinery as the local connectivity engine, then
adds an explicit handoff object for source/origin reachability across bands.

## Problem

The current production campaign answers a static-annulus question:

```text
Does any component connect geo_I to geo_O inside this annulus?
```

That is `ANY-SPAN` or `ANY-SHELL-MOAT` evidence. It is not source/origin
evidence, because a component can touch the geometric inner boundary without
being connected to the origin through all previously processed bands.

The source-propagation sidecar answers a different question:

```text
Does the certified source component survive from the processed prefix into the
next band, and if it dies, what source-connected inventory died with it?
```

## Terms

`CERTIFIED_SEED` is an atom that may be marked source-connected before a band is
processed. A seed is valid only when a prior proof, origin-prefix witness, or
explicit test fixture establishes its connection to the source. Geometric
membership in `geo_I` is not a certified seed by itself.

`SOURCE_CARRY` is the separator state emitted after processing a band:

```text
H_i = (carry_atoms, component_partition, source_bit_per_component,
       component_inventory)
```

`carry_atoms` are the atoms in the final radial guard window of width
`ceil(sqrt(K))`. `component_partition` records which carry atoms are already
connected through the processed prefix. `source_bit_per_component` marks which
carry components are connected to the certified source. `component_inventory`
preserves the atoms that belong to each component before non-carry atoms are
compacted away.

`SOURCE_TERMINAL` is the diagnostic state reached when a processed band had a
source-connected component but no source-connected carry component remains at
the outer guard. The terminal inventory is the complete inventory of the
retired source-connected component, not merely the final carry atoms.

`SOURCE_DEAD_CERT` is a claim-grade certificate. It requires a positive
coordinate Gaussian-prime source path to the claimed endpoint, a negative final
guard proving no legal continuation survives, bound BZ/schedule evidence,
artifact hashes, and a verifier-accepted terminal inventory summary. For very
large components, the inventory may be accepted through a claim-grade digest
accumulator instead of a literal JSON listing, but the accumulator must
explicitly identify itself as claim-grade evidence. A `SOURCE_TERMINAL`
diagnostic is necessary evidence for such a certificate but is not sufficient
by itself.

## Lemma 1: Local TileOp Equivalence

Within one radial band, the sidecar treats the existing TileOp machinery as the
local connectivity oracle. For a fixed `K`, radius interval, grid, and TileOp
configuration, the local graph atoms and edges are a CPU-side representation of
the same local connectivity relation used by the campaign machinery.

The sidecar may add source labels and separator bookkeeping around that graph,
but it must not reinterpret static-annulus verdicts as source claims. Local
TileOp equivalence is a local graph equivalence only.

## Lemma 2: Separator-State Sufficiency

For future bands, the processed prefix can affect connectivity only through
atoms within distance `sqrt(K)` of the next band. Therefore a carry window of
width `ceil(sqrt(K))` is sufficient: any future edge from an already processed
atom to an unprocessed atom must touch a carry atom.

The handoff must contain all carry atoms, not only source carry atoms. It must
also contain the partition of carry atoms and one source bit per partition
component:

```text
H_i = carry_atoms + component_partition + source_bit_per_component
```

Carrying only source atoms can lose later merges through non-source carry.
Carrying all carry atoms as source can invent reachability. Carrying the full
partition plus source bits preserves exactly what the prefix proved.

The implementation also carries `component_inventory` so terminal death can
report the retired source component after non-carry atoms have been compacted.

For coordinate atoms the carry predicate is the literal final guard
`[R - ceil(sqrt(K)), R]`. TileOp port atoms are abstract tile-support atoms,
not point primes. Their stable identity is the TileOp `(tile, face, ordinal)`,
and their stable radial support may overshoot the current band's `R` when the
same tile/port is needed to overlap the next independently tiled band. The
sidecar therefore allows TileOp port atoms, and only those abstract support
atoms, to remain carry-eligible when their stable support norm is above
`R^2` but still above the lower guard threshold. This preserves stable port
identity across independently built bands without weakening coordinate-source
terminal death.

## Lemma 3: No-Rewire

When the next band is processed, the incoming separator components may be wired
only according to `component_partition`. The next band may connect to those
carry atoms through real local edges, but it may not reassign source status from
geometric boundary flags or transient component IDs.

This prevents two false moves:

- false weld: merging components merely because they touch the same geometric
  side of a band;
- false source: treating every inner-boundary atom as source-connected after
  the first band.

Source reachability is data carried by `source_bit_per_component`, not a
property inferred from `geo_I` in later bands.

## Lemma 4: Composed-Band Equivalence

If every band emits an exact separator state and the next band imports it
without rewiring, then processing a sequence of stitched bands produces the same
separator state as processing the same atoms and edges as one big band.

The acceptance comparison is separator equality:

```text
canonical(carry_atoms, component_partition, source_bit_per_component,
          component_inventory)
```

It is not enough to compare only final `SPANNING`, `MOAT`, or "has source"
booleans. The fixtures must compare the carry partition and source bits, because
those are the state that future bands consume.

## Lemma 5: Terminal Guard And Death Logic

After a band ending at radius `R`, a source component is dead only when no
source-connected component intersects the final carry window
`[R - ceil(sqrt(K)), R]`. If there is no such source carry component, no future
prime outside radius `R` can be adjacent to the retired source component,
because every allowed edge has length at most `sqrt(K)`.

For TileOp-port continuation, "intersects the final carry window" is evaluated
through the abstract support rule above: a source-connected TileOp port with
stable tile support beyond `R` is still live evidence for the next band, not
proof of terminal death. Treating such an overhanging port as dead would create
a false terminal certificate at a band boundary.

For a coordinate-prefix to TileOp-port continuation, an unbridged coordinate
carry atom needs one more distinction:

- if there is no legal next-band Gaussian-prime candidate within distance
  `sqrt(K)`, the atom has no continuation into that band;
- if a legal next-band candidate exists and its local component has no encoded
  TileOp face ports, the candidate is a dead-end attachment inside the next
  band; it cannot carry source reachability farther, but claim-grade inventory
  must still account for it;
- if a legal next-band candidate exists and the bridge failure is not such a
  dead-end component, the continuation is unsafe for a source-death claim.

Thus a claim-grade continuation cannot require "all coordinate carry atoms must
bridge." That is too strong. It must require that every source-connected carry
atom either bridges into the next local graph or is proven to have no legal
next-band candidate or only dead-end candidates. Any unbridged
source-connected atom with an unsafe candidate is a stop condition for
`SOURCE_DEAD_CERT`.

The TileOp-port diagnostic runner reports this distinction explicitly:

```text
source_bridged_coordinate_carry_atoms
source_unbridged_without_next_band_candidates
source_unbridged_with_next_band_candidates
source_unbridged_dead_end_candidate_atoms
source_unbridged_unsafe_candidate_atoms
source_bridge_rejected_candidate_atoms
```

For a source-death certificate,
`source_unbridged_unsafe_candidate_atoms` must be zero. Non-source unbridged
carry atoms still matter for composed-band equivalence, but they do not by
themselves keep the source component alive.

## Lemma 6: Last-Band Transfer Sufficiency

For diagnostic lower-bound refinement, a dead progress row is not sufficient,
but all earlier band artifacts are not mathematically necessary. Let `H_L` be
the exact live separator at the inner cut of the first dead band, including all
source and neutral carry atoms, the full carry partition, and source bits. Let
`T_D` be a source-free transfer summary for the dead band `[R_L, R_D]`.

`T_D` must bind:

```text
K, R_L, R_D, carry width, schedule/oracle identity, overflow status,
inner boundary atoms or ports, outer continuation atoms or ports,
the local boundary partition induced by accepted local edges and bridges,
and per local boundary component coordinate max summaries.
```

Then `H_L + T_D` is enough to identify which local components in the dead band
are source-connected and to locate the furthest coordinate Gaussian-prime
candidate inside that band. The algorithm is:

1. Union the partition blocks from `H_L`.
2. Union the local boundary partition blocks from `T_D`.
3. Mark a root as source iff it contains an incoming source-bit component from
   `H_L`.
4. Death is confirmed iff no source root touches an outer continuation atom or
   valid TileOp port overhang.
5. The diagnostic furthest candidate is the maximum coordinate atom summary
   among source roots.

The incoming source is not inferred from `geo_I`. The `geo_I` and `geo_O`
surfaces in `T_D` bind the local annulus and guard predicates only. TileOp port
support may keep continuation alive, but a port support norm is not a coordinate
furthest atom.

The following weakenings are invalid:

- storing only source-marked ports or source-marked carry;
- dropping neutral carry classes, because they may weld to source inside the
  dead band;
- treating all carry as source;
- using a dead progress row without `H_L` or an equivalent boundary transfer;
- using port atoms as coordinate endpoint evidence without coordinate-path
  expansion.

## Lemma 7: First-Plus-Rolling-Last Campaign State

For source-survival execution, the only mathematical continuation state after a
cut is the exact live separator for that cut. Therefore a W-scale campaign does
not need to retain all prior band artifacts in the hot path.

The minimal campaign state is:

```text
first_source_artifact
current_live_handoff
previous_live_handoff
active_last_band_transfer_summary
terminal_summary_if_dead
```

`first_source_artifact` certifies the seed/source identity and global geometry.
`current_live_handoff` continues the next band. `previous_live_handoff` is kept
only while processing the active band, so that if the active band kills the
source, `previous_live_handoff + active_last_band_transfer_summary` forms the
local diagnostic refinement artifact.

Sparse checkpoints and older band summaries are operational restart evidence,
not mathematical continuation state. Claim-grade certificates may still require
targeted replay, coordinate path expansion, or an independent accumulator for
terminal coordinate inventory; those artifacts belong to the proof tier, not to
the hot continuation state.

## Engineering Constraints

- The carry width is `ceil(sqrt(K))`; for `K = 32`, this is `6`.
- Source reachability and geometric boundary flags remain separate.
- Source/origin proof rows reject overflow.
- Terminal inventory must preserve retired source components before compaction.
- Summary-only terminal inventory accumulators are non-claim evidence. A
  claim-grade accumulator must use an explicit claim-grade mode and claim-grade
  acceptance flag before it can substitute for a literal inventory listing. It
  must also attest that the terminal inventory stream was observed completely,
  emitted in canonical order, duplicate-free, finalized for the retired source
  component, and overflow-checked.
- Certificates must bind seed mode, geometry, commit, build, BZ/schedule
  evidence, artifact hashes, endpoint path, and terminal inventory.
- Static-annulus `ANY-SPAN` and `ANY-SHELL-MOAT` rows remain diagnostics for
  this protocol unless a separate source/origin certificate accepts them.
