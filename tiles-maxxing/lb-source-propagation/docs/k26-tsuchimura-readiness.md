# sqrt(26) Tsuchimura Readiness

This note is the execution guard for the first source/origin comparison target.
It is not a result claim.

## Target

- Claim label: `SOURCE_ORIGIN_K26`, not static-annulus `ANY-SPAN` or
  `ANY-SHELL-MOAT`.
- Tsuchimura endpoint: `943460 + 376039i`.
- Canonical-octant representative used by the sidecar runners:
  `376039 + 943460i`.
- Endpoint norm: `1031522101121`.
- Radius: about `1015638.765`.
- Expected component size: `14,542,615,005`.

For the exact mathematical guard, `R_final >= 1015644` puts the expected
endpoint below `R_final - sqrt(26)`.

For the current conservative integer carry shell
`R_final - ceil_sqrt(26) <= |p| <= R_final`, use `R_final >= 1015645`.
At `R_final = 1015644`, the endpoint is still inside the conservative carry
shell. Until a tighter exact non-square guard predicate is promoted into the
verifier, the campaign should use the conservative value.

## Required Evidence Before Run

1. Local sidecar CMake/CTest passes from a fresh build directory.
2. Full independent `verification/` CTest passes from a fresh build directory.
3. `k26_tsuchimura_preflight` passes locally and on remote smoke.
4. A remote 4090 sidecar smoke passes under the budget cap:
   - price `< 0.37 USD/hour`;
   - total budget `<= 1.50 USD`;
   - no long K32 launch.
5. The run is wired to source/origin seed logic, not `geo_I` flags.
6. Every source/origin proof row rejects overflow.
7. Non-square `K=26` has per-row BZ evidence, and every accepted row is
   BZ-clean. The exact schedule evidence shows the nominal 124-row
   width-8192 schedule is not BZ-clean at rows `15`, `58`, and `75`, then
   repairs the internal boundaries `122880`, `475136`, and `622592` down by
   `1`. The repaired schedule is BZ-clean, emits
   `BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE`, and carries digest
   `sha256:lb_source_k26_repaired_bz_schedule_v1:7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95`.
   This accepts the schedule evidence only; it is still not source/origin proof
   until an executed full-run profile binds the same digest.
8. Coordinate-to-port seam bridging is accepted or explicitly reported as
   diagnostic evidence. The current K36 smoke keeps `133` source coordinate
   carry atoms as the original incoming separator, inserts `374` bridge edges
   from `106` bridgeable coordinate carry atoms to `7` canonical port atoms,
   and leaves `27` coordinate carry atoms with no first-band port bridge. Until
   there is a theorem/verifier gate proving that this hybrid handoff preserves
   every future source attachment needed by the terminal guard, this cannot
   support a `SOURCE_DEAD_CERT`.

## Certificate Gap To Close

`SOURCE_TERMINAL` is not enough. A `SOURCE_DEAD_CERT` needs:

- positive source path or certificate chain to the canonical-octant
  representative `376039 + 943460i`, with explicit symmetry comparison to
  Tsuchimura's `943460 + 376039i`;
- negative final guard proof at `R_final >= 1015645` under the current
  conservative integer carry shell;
- terminal inventory with count/digest/max norm/tie set;
- stable artifact hashes for carry manifests and source profile drafts;
- commit/build identity and BZ evidence in the profile metadata.

The bundle-level acceptance gate is:

```bash
check_k26_full_run_bundle.sh OUT_DIR \
  --source-dead-checker source_dead_cert_check \
  --source-dead-gap-checker source_dead_gap_check
```

It expects `k26-prefix-result.json`, `k26-continuation-result.json`, the K26
command/profile/BZ evidence JSON, prefix manifest/witness, the
`k26-source-dead-gap.json` diagnostic, the
`k26-full-run-artifacts.sha256` hash manifest, and
`k26-source-dead-cert.json`. It rejects the bundle if any required artifact
hash does not match the manifest, if the BZ digest is not identical across
artifacts, if the gap artifact is malformed, if the
continuation did not run with `seam_bridge_policy=require_full_bridge`, if any
source coordinate carry atom remained unbridged, if TileOp overflow occurred,
if terminal source death was not reached at `R_final=1015645`, if the terminal
inventory count is not `14,542,615,005`, if the cert's terminal inventory
summary does not match the executed continuation count/digest/max-norm/tie-set,
if `endpoint_atom_id` is not the stable coordinate atom id
`1615075207964004` for the canonical endpoint,
if `metadata.artifact_hash` is not exactly the SHA-256 hash of
`k26-continuation-result.json`,
or if the independent `source_dead_cert_check` does not accept a listed
source-dead draft. A `summary_only_non_claim` cert remains useful diagnostic
shape evidence, but the bundle checker reports
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM` instead
of accepting the bundle.

The executable harness for producing the bundle shape is:

```bash
run_k26_full_source_bundle.sh \
  --build-dir BUILD_DIR \
  --out-dir OUT_DIR \
  --timeout-seconds 1200
```

It writes the command, BZ, profile, prefix, and continuation artifacts using
the strict `--require-full-bridge` continuation schedule. It is deliberately
certificate-gated: without a supplied `k26-source-dead-cert.json` it still
writes `k26-prefix-progress.jsonl`, `k26-source-dead-gap.json`, a partial
`k26-full-run-artifacts.sha256` manifest, and stops with
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING`. The gap artifact binds
the continuation artifact and records the exact missing layer: a coordinate
Gaussian-prime source path from the origin prefix to the canonical endpoint,
plus a verifier that expands the mixed coordinate/port atom chain into
claim-grade coordinate provenance. It also binds the repaired K26 BZ schedule
digest as accepted-for-schedule but not accepted-for-claim evidence. When
invoked with
`--source-dead-gap-checker`, the harness verifies this gap artifact with the
independent `source_dead_gap_check` before reporting the missing cert blocker.
The prefix-progress JSONL rows are operational telemetry only: they expose
band radii, generated atom counts, edge counts, source carry/death state, and
phase timings so a paid run can be stopped with evidence instead of guesswork.
They do not relax the `SOURCE_DEAD_CERT` gate.
With a supplied cert, the manifest binds the cert with the
command/profile/BZ/prefix/progress/continuation/gap artifacts before the
checker runs.
This keeps a paid sqrt(26) attempt reproducible without relaxing the
`SOURCE_DEAD_CERT` logic. `--timeout-seconds` should be set for paid runs so a
slow prefix or continuation exits with an explicit timeout status before the
runtime budget is exceeded. If the supplied cert is only a summary accumulator
non-claim, the harness writes
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM` to
`status.txt` and exits nonzero.

## Executable Contract

The local executable contract is:

```bash
k26_source_run_contract
```

It emits `lb_source_k26_run_contract_v1`, including the target endpoint,
`R_final >= 1015645` conservative guard, a suggested 8192-radius band schedule,
required evidence, and current blocking gaps. It must keep
`"executable_now": false` until these pieces exist:

- a full-scale K26 source runner that feeds the sidecar from campaign TileOps
  using the same stable coordinate/canonical-port identity as the diagnostic
  CPU TileOp-fed runner;
- execution of the repaired variable-boundary schedule by the TileOp-port
  runner. The runner now supports explicit boundaries, but no K26 full source
  run has consumed the repaired schedule yet;
- promotion of the TileOp port-graph primitive into the full band scheduler;
  transient TileOp group labels must remain internal only. The current
  `source_tileop_port_runner` can consume an origin-prefix manifest/witness and
  bridge coordinate carry into canonical TileOp port atoms. The handoff is
  hybrid: the original coordinate separator remains incoming and bridge edges
  connect it to first-band TileOp ports. It is still diagnostic because the
  smoke reaches terminal source death after one port band.
- an accepted seam-bridge rule for moving from coordinate carry atoms into the
  TileOp-port graph. The runner reports `bridged_coordinate_carry_atoms`,
  `unbridged_coordinate_carry_atoms`, `bridged_port_carry_atoms`, and
  `bridge_edges`; K26 remains blocked until those fields are justified by an
  accepted lemma and verifier gate. The `--require-full-bridge` runner mode is
  the strict executable guard: it rejects a manifest handoff whenever any source
  coordinate carry atom has no first-band TileOp-port bridge. K26 remains
  diagnostic until it either satisfies that strict guard or an accepted
  verifier/theorem explains why the unbridged carry atoms cannot affect the
  source/death certificate.
- accepted terminal inventory handling for count/digest/max norm/tie set at
  14.5B-member scale;
- accepted K26 non-square BZ evidence bound to the repaired schedule digest;
- an accepted full-scale `SOURCE_DEAD_CERT` artifact. The current independent
  draft checker has two deliberately separate modes: a listed-inventory mode
  that recomputes the count/digest/max-norm/tie-set from the explicit terminal
  inventory, and a `summary_only_non_claim` mode that only validates a
  Tsuchimura-scale accumulator shape plus extrema witnesses and emits
  `SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM_PASS`. The latter is the scalable
  K26 contract shape, not a certificate acceptance; the real K26 chain,
  accumulator provenance, and full-run BZ digest binding are still missing.

The exact command contract is:

```bash
k26_source_run_commands
```

It emits the row-0 coordinate prefix command and the repaired TileOp-port
continuation schedule. The row-0 prefix command targets the canonical-octant
representative `376039 + 943460i`; the comparison back to Tsuchimura's
`943460 + 376039i` is by Gaussian-unit and conjugation symmetry. The
continuation command includes `--require-full-bridge` and
`--target-a 376039 --target-b 943460`, so unbridged source coordinate carry
atoms are a hard stop and the canonical endpoint must be bridged into a
TileOp-port component and reported as source-reached in the continuation
artifact. The target field currently reports
`path_provenance=mixed_coordinate_port_atom_chain_non_claim` plus a stable atom
id chain when target reachability is proved. This is still not a coordinate
Gaussian-prime source-path witness suitable for a `SOURCE_DEAD_CERT`. This
command is not part of the local smoke gate. A Mac Mini probe of the row-0
prefix now completes after the prefix-witness BFS-tree fix: on 2026-05-22,
`source_origin_cpu_runner --k-sq 26 --r-final 8192 --band-width 8192
--endpoint-a 376039 --endpoint-b 943460 --max-atoms 50000000
--manifest-out ... --prefix-witness-out ... --progress-out ...` completed
locally in about `40s`. It generated `1,979,012` atoms, `6,328,416` edges,
`2,580` source carry witness targets, and wrote the prefix manifest plus
witness. This is operational readiness evidence for row 0 only; it is still
not a source/origin comparison, not terminal source death, and not a
`SOURCE_DEAD_CERT`.

## Stop Conditions

Stop and report without claiming reproduction if:

- source state depends on transient TileOp group labels or union-find roots;
- any overflow appears in a source/origin row;
- coordinate-to-port seam bridging leaves unbridged coordinate carry atoms or
  first-band port death without an accepted proof that this is irrelevant;
- BZ evidence is missing or mismatched for `K=26`;
- terminal inventory cannot preserve retired source components;
- remote runtime or memory threatens the agreed budget.
