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
   diagnostic evidence. The bridge distinction needed for a death certificate
   is not "every coordinate carry atom bridges." The needed statement is:
   every source-connected coordinate carry atom either bridges into the
   TileOp-port graph, has no legal next-band Gaussian-prime candidate within
   `sqrt(K)`, or has only dead-end candidates whose local components have no
   encoded TileOp face ports. The tiny K36 smoke has the clean shape: `82`
   source carry atoms bridge and `51` source carry atoms have no next-band
   candidate. K26 row 0 to row 1 now has the corresponding unsafe-candidate
   shape: a 2026-05-23 local diagnostic reports `1369` source carry atoms
   bridged, `1154` source carry atoms unbridged with no candidate, `57` source
   carry atoms unbridged with dead-end candidates, and
   `source_unbridged_unsafe_candidate_atoms=0`. This supports the bridge-safety
   side of the diagnostic, but not yet a `SOURCE_DEAD_CERT`.

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
artifacts, if the gap artifact is malformed, if the continuation did not run
with `seam_bridge_policy=diagnostic_allow_unbridged`, if any
source-connected coordinate carry atom has an unsafe unbridged next-band
candidate, if TileOp overflow occurred, if terminal source death was not
reached at `R_final=1015645`, if the terminal inventory count is not
`14,542,615,005`, if the cert's terminal inventory summary does not match the
executed continuation count/digest/max-norm/tie-set,
if `endpoint_atom_id` is not the stable coordinate atom id
`1615075207964004` for the canonical endpoint,
if `metadata.artifact_hash` is not exactly the SHA-256 hash of
`k26-continuation-result.json`, or if the independent
`source_dead_cert_check` does not accept a listed source-dead draft. The
`k26-source-dead-gap.json` layer also machine-checks three explicit
obligation objects: `bridge_safety` must show
`source_unbridged_unsafe_candidate_atoms=0`; `coordinate_path_obligation`
must record that the observed target path is still
`mixed_coordinate_port_atom_chain_non_claim`, not
`coordinate_gaussian_prime_path`; and `terminal_inventory_obligation` must
record that the observed inventory is `summary_digest_only_non_claim`, not
claim-grade listed/proven terminal inventory. A `summary_only_non_claim` cert
remains useful diagnostic shape evidence, but the bundle checker reports
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
the repaired continuation schedule and the unsafe-candidate bridge gate. It is
deliberately certificate-gated: without a supplied `k26-source-dead-cert.json`
it still
writes `k26-prefix-progress.jsonl`, `k26-continuation-progress.jsonl`,
`k26-source-dead-gap.json`, a partial
`k26-full-run-artifacts.sha256` manifest, and stops with
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING`. The gap artifact binds
the continuation artifact and records the exact remaining certificate
obligations: bridge safety, coordinate path, terminal inventory, and repaired
K26 BZ schedule evidence. The bridge obligation can pass as diagnostic evidence
when every source-connected carry atom either bridges, has no legal next-band
candidate, or has only dead-end candidates; the coordinate path obligation
remains blocked until the mixed coordinate/port atom chain is expanded into a
coordinate Gaussian-prime source path from the origin prefix to the canonical
endpoint; the terminal inventory obligation remains blocked until the summary
digest/count/max-norm evidence is promoted to claim-grade inventory
provenance. The BZ digest is bound as accepted-for-schedule but not
accepted-for-claim evidence. When
invoked with
`--source-dead-gap-checker`, the harness verifies this gap artifact with the
independent `source_dead_gap_check` before reporting the missing cert blocker.
The prefix-progress JSONL rows are operational telemetry only: they expose
band radii, generated atom counts, edge counts, source carry/death state, and
phase timings so a paid run can be stopped with evidence instead of guesswork.
The continuation-progress JSONL rows do the same for TileOp-port continuation
bands, including tile counts, port graph counts, overflow totals, seam bridge
counts, source carry/death state, and timings. Neither progress artifact
relaxes the `SOURCE_DEAD_CERT` gate.
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

- accepted full-run K26 bundle artifacts. The bundle harness now wires the
  row-0 coordinate prefix into rows `1..123` of TileOp-port continuation using
  stable coordinate/canonical-port identity, but no budgeted K26 execution has
  completed the repaired schedule and produced accepted artifacts yet;
- execution evidence for the repaired variable-boundary schedule. The
  TileOp-port runner supports explicit boundaries and the harness passes the
  repaired schedule, but the full schedule has not completed under budget yet;
- promotion of the TileOp port-graph primitive into the full band scheduler;
  transient TileOp group labels must remain internal only. The current
  `source_tileop_port_runner` can consume an origin-prefix manifest/witness and
  bridge coordinate carry into canonical TileOp port atoms. The handoff is
  hybrid: the original coordinate separator remains incoming and bridge edges
  connect it to first-band TileOp ports. It is still diagnostic because the
  smoke reaches terminal source death after one port band.
- an accepted seam-bridge rule for moving from coordinate carry atoms into the
  TileOp-port graph. The runner reports `bridged_coordinate_carry_atoms`,
  `unbridged_coordinate_carry_atoms`, next-band candidate counters,
  source-only versions of those counters, `bridged_port_carry_atoms`, and
  `bridge_edges`; K26 remains blocked until those fields are justified by an
  accepted lemma and verifier gate. The old `--require-full-bridge` guard is a
  conservative hard stop, but the current certificate logic is sharper:
  `source_unbridged_unsafe_candidate_atoms` must be zero. A K26 first
  continuation diagnostic on 2026-05-23 reported
  `source_unbridged_with_next_band_candidates=57`, all classified as
  `source_unbridged_dead_end_candidate_atoms=57`, with
  `source_unbridged_unsafe_candidate_atoms=0`. This closes the bridge-safety
  blocker for row 0 -> row 1, but not the terminal inventory or coordinate
  endpoint-path blockers.
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
continuation command includes `--target-a 376039 --target-b 943460` and the
bundle checker requires `source_unbridged_unsafe_candidate_atoms=0`. Unbridged
source carry is acceptable only when there is no legal next-band candidate or
only dead-end candidates with no encoded face ports. The canonical endpoint
must still be bridged into a TileOp-port component and reported as
source-reached in the continuation artifact. The target field currently reports
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

A follow-up local first-continuation diagnostic over `8192 -> 16384` with the
same prefix artifacts completed in about `470s` and reached
`terminal_source_dead=true` as a diagnostic. Its seam counters were:
`coordinate_carry_atoms_with_next_band_candidates=1504`,
`bridged_coordinate_carry_atoms=1441`,
`unbridged_coordinate_carry_atoms=1249`,
`unbridged_without_next_band_candidates=1186`,
`unbridged_with_next_band_candidates=63`,
`source_coordinate_carry_atoms_with_next_band_candidates=1426`,
`source_bridged_coordinate_carry_atoms=1369`,
`source_unbridged_coordinate_carry_atoms=1211`,
`source_unbridged_without_next_band_candidates=1154`,
`source_unbridged_with_next_band_candidates=57`,
`source_unbridged_dead_end_candidate_atoms=57`,
`source_unbridged_unsafe_candidate_atoms=0`, and
`source_bridge_rejected_candidate_atoms=72`. The only rejection reason was
`visible coordinate component has no encoded face ports`. This is useful
bridge-safety evidence, but it is still not a certificate because the
coordinate endpoint path and claim-grade terminal inventory are missing.

## Stop Conditions

Stop and report without claiming reproduction if:

- source state depends on transient TileOp group labels or union-find roots;
- any overflow appears in a source/origin row;
- coordinate-to-port seam bridging leaves source-connected unbridged carry
  atoms with legal next-band candidates;
- BZ evidence is missing or mismatched for `K=26`;
- terminal inventory cannot preserve retired source components;
- remote runtime or memory threatens the agreed budget.
