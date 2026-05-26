# LB Source Propagation Sidecar

This is the Phase 1 CPU sidecar for lower-bound source propagation. It is not a
replacement for the existing TileOp/CUDA campaign; it is the source-stitching
protocol plus smoke contact with the existing CPU TileOp producer.

The proof model is documented in
`../../methodology/source-propagation-band-stitching.md`.

## Current LB Contract

LB source propagation is not a static-annulus verdict. It carries one certified
source signal across stacked bands:

```text
H_i = carry_atoms + component_partition + source_bit_per_component
```

- `carry_atoms`: every stable coordinate or port frontier atom that can affect
  future connectivity, source or neutral.
- `component_partition`: exact prefix connectivity among those carry atoms.
- `source_bit_per_component`: which partition classes are connected to the
  certified source.

Each band imports `H_{i-1}`, applies local TileOp connectivity plus accepted
bridges, and emits `H_i`. Later bands never reseed from `geo_I`.

The hot coarse sweep must carry bounded live frontier state only. Historical
`component_inventory`, proof ledgers, terminal accumulators, BZ bindings,
coordinate paths, K26 bundle artifacts, full TileOp slabs, and whole-band port
graphs are proof-tier or diagnostic evidence, not hot state.

The intended campaign shape is first-plus-rolling-last:

```text
first_source_artifact
current_live_handoff
previous_live_handoff
active_last_band_transfer_summary
terminal_summary_if_dead
```

The coarse sweep finds the last-live / first-dead window. Expensive proof work
is on-demand in that window. All terminal/progress outputs remain
`DIAGNOSTIC_NON_CLAIM` until independent path, bridge, BZ, negative-guard, and
inventory gates pass.

Current first-read docs:

1. `../../methodology/source-propagation-band-stitching.md`
2. `docs/lb-code-alignment-goal.md` before code-alignment work
3. `docs/code-quality-guidelines.md` before code changes
4. `docs/agentic-goal-loop-guidelines.md` before autonomous or remote loops
5. `docs/lb-handoff-redesign.md`
6. `docs/tile-frontier-streaming-redesign.md` when touching streaming
7. `docs/k26-tsuchimura-readiness.md` when touching K26
8. Remote/overnight runbooks only when paid remote execution is explicitly
   authorized.

The sidecar models a band handoff as:

```text
H_i = (carry_atoms, component_partition, source_bit_per_component)
```

For certificate inventory it also carries per-component payloads. Those payloads
do not create connectivity; they preserve retired vertices so terminal source
death can report where the source component ended.

## Live Handoff, Carry Manifest, And Draft Output

The sidecar exposes `coordinate_atom_id(a, b)` /
`decode_coordinate_atom_id(id)` for first-quadrant source runs. This keeps
small runners, TileOp producer smokes, carry manifests, and future certificate
chains on one stable coordinate identity instead of app-local encodings or
transient union-find roots.

It also exposes `port_atom_id(tile_i, tile_j, face, ordinal)` /
`decode_port_atom_id(id)` for campaign-scale carry state. Port atoms are
negative ids and encode only canonical TileOp port position. They deliberately
do not encode transient per-tile group labels, so future K26 runners can carry
TileOp-compressed boundary state without depending on local union-find names.

The current LB continuation hot path uses `LB_SOURCE_LIVE_HANDOFF_V1`, which
stores only live separator state. The older deterministic carry-manifest helpers
remain available for small diagnostics and compatibility tests:

- `make_carry_manifest(k_sq, outer_radius, result)`
- `write_carry_manifest(...)` / `read_carry_manifest(...)`
- `carry_manifest_to_string(...)` / `carry_manifest_from_string(...)`

The text format is line-oriented and starts with
`LB_SOURCE_CARRY_MANIFEST_V1`. It stores `k_sq`, `outer_radius`, `carry_width`,
canonical carry atoms, canonical component partitions, source bits, and
per-component inventory.

Draft JSON emitters are also available for profile/certificate plumbing:

- `source_profile_draft_json(...)`
- `source_certificate_draft_json(...)`
- `summarize_inventory(...)`

These are sidecar draft artifacts only. They make the source mode, geometry,
build/BZ placeholders, handoff state, terminal guard state, and terminal
inventory explicit. Terminal inventory now carries a canonical
`sha256:lb_source_inventory_v1` count/digest summary in addition to explicit
small-run atom ids; it is still not a final source proof schema.

## CPU TileOp Smoke

The `source_prop_cpu_tileop_smoke` CTest target links the existing
`cpp-campaign-v2` `campaign` library, builds the small K36 axis-prime fixture,
calls `campaign::process_tile`, derives coordinate-stable source atoms from
`campaign::sieve_tile`, applies an `ORIGIN_SOURCE` seed, runs the sidecar source
propagator, and round-trips the carry manifest.

This is intentionally a smoke path. `TileOp` group labels are not persisted as
source carry atoms because they are tile-local; campaign-scale source claims
must use stable coordinate or canonical-port atoms.

`lb_source/tileop_port_graph.h` is the first campaign-scale bridge primitive.
It converts a batch of TileOps into a `BandInput` over canonical port atoms:
ports with the same local TileOp group label become same-tile edges, adjacent
I/O and L/R face ordinals become seam edges, and any `OVERFLOW_BIT` tile marks
the band as `force_overflow` so source claims reject rather than silently
stitch through bad evidence. This is still a primitive, not the full K26 runner:
it does not yet schedule the 124 K26 bands or bind terminal inventory/BZ
evidence.

The same header also exposes `bridge_coordinate_prime_to_ports(...)`, a
diagnostic bridge from a concrete sieved coordinate prime to the canonical
TileOp port atoms carrying its local visible component. The helper recomputes
the TileOp byte payload from `(coord, constants, primes)` before returning
ports, so stale or mismatched TileOps cannot silently seed source carry. It
returns canonical port atoms only; local TileOp group labels remain transient
and must not be persisted.

`source_tileop_cpu_runner` is the next diagnostic bridge: it builds campaign
`Grid` objects per radial band, calls `campaign::process_tile` on each active
tile as the TileOp contact point, deduplicates `campaign::sieve_tile` Gaussian
primes into stable coordinate atom ids, and then stitches those bands through
the sidecar. This is still a CPU diagnostic, but it proves the sidecar can be
fed from existing campaign TileOp production surfaces without changing current
campaign verdict semantics.

The TileOp-fed runner can also start from a live handoff emitted by
`source_origin_cpu_runner --live-manifest-out`. That is the intended handoff shape
for K26: the coordinate-fed prefix certifies the origin component up to a radius
where campaign `Grid` preconditions hold, then campaign TileOp bands continue
from the exact separator state instead of inventing a new source seed. For
diagnostic positive-witness checks, the prefix runner can also emit
`--prefix-witness-out`; the TileOp-fed runner consumes it with
`--prefix-witness-in` and splices the origin-prefix path to the continuation
path.

`source_tileop_port_runner` schedules radial bands through the TileOp port
graph directly. Its smoke mode seeds the first band from `geo_I` inner flags and
is intentionally labeled `GEO_I_PORT_DIAGNOSTIC`; on the tiny K36 fixture this
source dies before the final carry shell, which is useful evidence that the
runner reports terminal death instead of inventing a source seed.
It can also consume `--live-manifest-in` plus `--prefix-witness-in` from the
coordinate-fed prefix runner. In that mode it keeps the original coordinate
separator as the incoming state, then adds bridge edges from coordinate carry
atoms to first-band TileOp port atoms by looking for TileOp-band primes within
distance `sqrt(K)` of each coordinate carry atom. Coordinate carry atoms with
no first-band port bridge are split into "no legal next-band candidate" and
"candidate existed but no accepted bridge" counters, with source-only versions
of the same counters. This is still diagnostic: the seam bridge is explicit
evidence for the next engineering gate, not an accepted source/death
certificate.
The runner also has `--require-full-bridge`, which rejects a live handoff
when any coordinate carry atom lacks a first-band TileOp-port bridge. That
strict mode is a conservative diagnostic guardrail, not the K26 run contract.
The sharper certificate condition is
`source_unbridged_unsafe_candidate_atoms == 0`: source-connected carry atoms
may be unbridged only when no legal next-band candidate exists or when every
candidate is a dead-end TileOp component with no encoded face ports. K26 still
remains diagnostic until the corresponding terminal inventory and coordinate
path layers are certificate-grade.
With `--target-a/--target-b`, the runner also inserts a canonical coordinate
target atom when the target prime is seen in a TileOp band, bridges it to its
visible TileOp-port component, and reports whether that coordinate target is in
the propagated source inventory. This is target reachability plumbing, not a
full source path certificate.

## Small Source Runner

`source_origin_cpu_runner` is a small-radius diagnostic runner. It enumerates
canonical-octant Gaussian-prime coordinates directly, seeds `Omega` by the
origin rule `norm_sq <= K`, uses stable coordinate atom ids, stitches radial
bands through `lb_source::process_band`, and emits
`lb_source_origin_cpu_runner_v1` JSON with source inventory count, digest, and
maximum observed source norm. When the diagnostic endpoint is source-reached,
it also emits a deterministic `source_path` from a certified origin seed.
With `--cert-out`, it writes a diagnostic `lb_source_dead_cert_draft_v1` only
when the run has accepted terminal source death, a reached endpoint, a source
path, and terminal inventory.
With `--live-manifest-out`, it writes the live carry separator when source survives
into the final carry window, allowing the TileOp-fed runner to continue from
the prefix without changing source semantics. With `--prefix-witness-out`, it
also writes line-oriented origin-prefix paths to each live source carry atom, so
the live handoff bridge can prove a positive path rather than only propagate a
source bit.

This closes the first executable gap between the abstract sidecar protocol and
a source/origin run, but it is still a non-claim surface. It is not TileOp/CUDA
fed at campaign scale, it does not process side-boundary separator state, and it
does not emit an accepted `SOURCE_DEAD_CERT`.

## Local Gate

From the repo root:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_phase1_local_gates.sh
```

This runs fresh local sidecar CMake/CTest, fresh independent `verification/`
CMake/CTest, `git diff --check`, the Phase 1 diff-scope guard, and the same
artifact checker used after a pulled Vast smoke. To run only the branch-scope
gate:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_phase1_diff_scope.sh
```

To run only the narrow pre-Vast stitching parity gate:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_phase1_parity_gate.sh
```

The parity gate checks five-, ten-, and twenty-band composed runs versus one big
band in the independent verifier. The broader local gate also covers false
welding, source-only-carry loss, neutral partition merges, terminal inventory,
hard overflow rejection, `K=32` carry width, associativity across band
groupings, certified source seed application/rejection, carry-manifest
round-trip/rejection, exact draft JSON output, CPU TileOp producer smoke, and
sqrt(26) Tsuchimura preflight/run-contract constants.

## Remote And K26 Runbooks

Remote execution and K26 Tsuchimura comparison are not first-read architecture.
They are conditional runbook surfaces:

- `docs/k26-tsuchimura-readiness.md` owns the K26 target, acceptance blockers,
  runtime-budget guard, and source-death certificate gap.
- `docs/archive/lb-source-propagation-optimization-plan-20260524.md` is a
  historical overnight 4090 plan, not active strategy or standing compute
  authorization.
- Remote scripts under `scripts/` are usable only when the current session
  explicitly authorizes paid execution.

Do not promote remote timing, static reach, K26 bundle output, or summary-only
source-death artifacts into LB proof evidence. They remain diagnostic until an
independent `SOURCE_DEAD_CERT` gate exists and passes.

## Integration Boundary

This protocol should be fed from the existing CPU/CUDA TileOp band machinery
without changing current static-annulus verdict semantics. Stable carry atoms
must be coordinates or canonical port atoms, not transient union-find roots.
