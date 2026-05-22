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
artifact hashes, and a verifier-accepted terminal inventory summary. A
`SOURCE_TERMINAL` diagnostic is necessary evidence for such a certificate but
is not sufficient by itself.

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

## Engineering Constraints

- The carry width is `ceil(sqrt(K))`; for `K = 32`, this is `6`.
- Source reachability and geometric boundary flags remain separate.
- Source/origin proof rows reject overflow.
- Terminal inventory must preserve retired source components before compaction.
- Certificates must bind seed mode, geometry, commit, build, BZ/schedule
  evidence, artifact hashes, endpoint path, and terminal inventory.
- Static-annulus `ANY-SPAN` and `ANY-SHELL-MOAT` rows remain diagnostics for
  this protocol unless a separate source/origin certificate accepts them.
