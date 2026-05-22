# LB Source Propagation Sidecar

This is the Phase 1 CPU sidecar for lower-bound source propagation. It is not a
replacement for the existing TileOp/CUDA campaign; it is the source-stitching
protocol plus smoke contact with the existing CPU TileOp producer.

The sidecar models a band handoff as:

```text
H_i = (carry_atoms, component_partition, source_bit_per_component)
```

For certificate inventory it also carries per-component payloads. Those payloads
do not create connectivity; they preserve retired vertices so terminal source
death can report where the source component ended.

## Carry Manifest And Draft Output

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

The library exposes deterministic carry-manifest helpers:

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
build/BZ placeholders, carry manifest, terminal guard state, and terminal
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

The TileOp-fed runner can also start from a carry manifest emitted by
`source_origin_cpu_runner --manifest-out`. That is the intended handoff shape
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
runner reports terminal death instead of inventing a live source manifest.
It can also consume `--manifest-in` plus `--prefix-witness-in` from the
coordinate-fed prefix runner. In that mode it keeps the original coordinate
separator as the incoming state, then adds bridge edges from coordinate carry
atoms to first-band TileOp port atoms by looking for TileOp-band primes within
distance `sqrt(K)` of each coordinate carry atom. Coordinate carry atoms with
no first-band port bridge are reported in the JSON. This is still diagnostic:
the seam bridge is explicit evidence for the next engineering gate, not an
accepted source/death certificate.

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
With `--manifest-out`, it writes the live carry separator when source survives
into the final carry window, allowing the TileOp-fed runner to continue from
the prefix without changing source semantics. With `--prefix-witness-out`, it
also writes line-oriented origin-prefix paths to each live source carry atom, so
the manifest bridge can prove a positive path rather than only propagate a
source bit.

This closes the first executable gap between the abstract sidecar protocol and
a source/origin run, but it is still a non-claim surface. It is not TileOp/CUDA
fed at campaign scale, it does not process side-boundary separator state, and it
does not emit an accepted `SOURCE_DEAD_CERT`.

## Local Gate

From the repo root:

```bash
cmake -S tiles-maxxing/lb-source-propagation -B /tmp/gm-lbsp
cmake --build /tmp/gm-lbsp -j
ctest --test-dir /tmp/gm-lbsp --output-on-failure
```

The tests cover composed bands versus one big band, false welding,
source-only-carry loss, neutral partition merges, terminal inventory, hard
overflow rejection, `K=32` carry width, associativity across band groupings,
certified source seed application/rejection, carry-manifest round-trip/rejection,
exact draft JSON output, CPU TileOp producer smoke, and sqrt(26) Tsuchimura
preflight/run-contract constants.

## Remote Smoke

After a qualifying Vast 4090 is rented and the repo is copied to the host, run:

```bash
cd /workspace/gaussian-moat-cuda
tiles-maxxing/lb-source-propagation/scripts/remote_sidecar_smoke.sh \
  --repo /workspace/gaussian-moat-cuda \
  --k-sq 26 \
  --build-dir /tmp/gm-lbsp-remote-smoke \
  --out-dir /workspace/lb-source-remote-smoke
```

This builds/tests the sidecar, runs the independent `verification/` CTest
suite, and runs the CPU TileOp source smoke. It does not start K32, does not
run a long campaign, and does not claim a moat result. The smoke artifact set
includes `source_origin_cpu_runner_smoke.json`, `k26_source_run_contract.json`,
`k26_execution_plan` output, `k26_bz_schedule_check.json`, and
`k26_source_run_profile.json`, plus `k26_source_run_commands.json` with the
exact repaired continuation schedule. These must remain non-claim artifacts
until the blockers listed in the K26 contract are closed.

The remote smoke finishes by running:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_remote_smoke_artifacts.sh \
  /workspace/lb-source-remote-smoke --expect-k-sq 26
```

The checker has its own CTest self-test so stale test counts, executable K26
plan claims, and missing provenance checks fail locally before a paid run.
After pulling Vast artifacts, run the same checker with `--expect-head` and
`--expect-branch` to bind the smoke to the deployed local source.

The sqrt(26) readiness guard lives in
`docs/k26-tsuchimura-readiness.md`.

The exact non-claim command contract for the eventual K26 run is emitted by:

```bash
k26_source_run_commands
```

It prints the origin-prefix command and the repaired
`source_tileop_port_runner --schedule-radii ...` continuation command. The
output is a run contract only; it uses the canonical-octant representative
`376039 + 943460i` for Tsuchimura's endpoint `943460 + 376039i`, does not
execute the full sqrt(26) comparison, and must not be treated as a
`SOURCE_DEAD_CERT`.

`k26_source_run_contract` emits the execution contract for the Tsuchimura
comparison target. It is intentionally a non-claim artifact:
`"executable_now": false` remains correct until a campaign-scale source runner,
scalable terminal inventory digest, accepted K26 BZ evidence, and
`SOURCE_DEAD_CERT` verifier exist.

`k26_execution_plan` emits the machine-checkable execution plan for the same
target. It expands the conservative `R_final=1015645` guard into 124 radial
rows at preferred width 8192, records the final row width 8029, binds the active
Vast budget caps, and uses the BZ-repaired row boundaries emitted by
`k26_bz_schedule_check`. This is still a non-claim artifact and must keep
`"executable_now": false` until the K26 blockers are closed.

`k26_bz_schedule_check` is an exact integer diagnostic for K26 non-square
bad-zone reconciliation. It deliberately does not make a claim. It records that
the nominal 124-row, width-8192 schedule is not BZ-clean: rows `15`, `58`, and
`75` contain Gaussian-prime norms in a bad zone. It then emits a repaired
schedule using the nearest clean internal boundary, choosing negative delta
before positive on ties. The current repaired boundaries shift `122880`,
`475136`, and `622592` down by `1`; all repaired rows are BZ-clean.

`k26_source_run_profile` binds the repaired schedule to the intended full-run
shape: exact coordinate prefix for row `0`, then TileOp-port continuation for
rows `1..123` from an origin-prefix manifest and witness. It also records the
next concrete implementation gap: the TileOp-port runner can now consume
explicit variable boundaries, but no full K26 source run has executed that
schedule yet.

## Integration Boundary

Phase 2 should feed this protocol from the existing CPU TileOp/band machinery
without changing current static-annulus verdict semantics. Stable carry atoms
must be coordinates or canonical port atoms, not transient union-find roots.
