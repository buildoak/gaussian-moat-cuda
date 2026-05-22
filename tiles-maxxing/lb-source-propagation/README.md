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
source carry atoms because they are tile-local; Phase 2 should promote stable
coordinate or canonical-port atoms before running campaign-scale source claims.

## Small Source Runner

`source_origin_cpu_runner` is a small-radius diagnostic runner. It enumerates
canonical-octant Gaussian-prime coordinates directly, seeds `Omega` by the
origin rule `norm_sq <= K`, uses stable coordinate atom ids, stitches radial
bands through `lb_source::process_band`, and emits
`lb_source_origin_cpu_runner_v1` JSON with source inventory count, digest, and
maximum observed source norm. When the diagnostic endpoint is source-reached,
it also emits a deterministic `source_path` from a certified origin seed.

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
  --build-dir /tmp/gm-lbsp-remote-smoke \
  --out-dir /workspace/lb-source-remote-smoke
```

This only builds/tests the sidecar and runs the CPU TileOp source smoke. It
does not start K32, does not run a long campaign, and does not claim a moat
result. The smoke artifact set includes `source_origin_cpu_runner_smoke.json`
and `k26_source_run_contract.json`, which must remain non-claim artifacts until
the blockers listed in the K26 contract are closed.

The sqrt(26) readiness guard lives in
`docs/k26-tsuchimura-readiness.md`.

`k26_source_run_contract` emits the execution contract for the Tsuchimura
comparison target. It is intentionally a non-claim artifact:
`"executable_now": false` remains correct until a campaign-scale source runner,
scalable terminal inventory digest, accepted K26 BZ evidence, and
`SOURCE_DEAD_CERT` verifier exist.

## Integration Boundary

Phase 2 should feed this protocol from the existing CPU TileOp/band machinery
without changing current static-annulus verdict semantics. Stable carry atoms
must be coordinates or canonical port atoms, not transient union-find roots.
