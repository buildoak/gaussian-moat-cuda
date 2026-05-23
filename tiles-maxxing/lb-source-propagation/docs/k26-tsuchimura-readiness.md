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
`k26-source-dead-gap.json` layer also machine-checks explicit obligation
objects: `bridge_safety` must show
`source_unbridged_unsafe_candidate_atoms=0`; `coordinate_path_obligation`
must record why the observed target evidence is not
`coordinate_gaussian_prime_path`; `terminal_inventory_obligation` must record
that the observed inventory is `summary_digest_only_non_claim`, not
claim-grade listed/proven terminal inventory; and the BZ schedule is bound as
accepted-for-schedule but not accepted-for-claim evidence. A reached diagnostic
target is blocked by `mixed_coordinate_port_atom_chain_non_claim`; a terminal
diagnostic that never reaches the target is blocked by
`SOURCE_DEAD_CERT_TARGET_NOT_REACHED`. If source carry is still live at
`R_final`, the harness reports `K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_STILL_LIVE`
and does not write a source-dead gap. A `summary_only_non_claim` cert remains
useful diagnostic shape evidence, but the bundle checker reports
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM` instead
of accepting the bundle.

The executable harness for producing the bundle shape is:

```bash
run_k26_full_source_bundle.sh \
  --build-dir BUILD_DIR \
  --out-dir OUT_DIR \
  --continuation-chunk-bands 8 \
  --resume-existing \
  --timeout-seconds 1200
```

It writes the command, BZ, profile, prefix, and continuation artifacts using
the repaired continuation schedule and the unsafe-candidate bridge gate. It is
deliberately certificate-gated: without a supplied `k26-source-dead-cert.json`
it still writes `k26-prefix-progress.jsonl`,
`k26-continuation-progress.jsonl`, `k26-source-dead-gap.json` when terminal
source death is reached, and a partial `k26-full-run-artifacts.sha256`
manifest. If terminal source death occurs before the canonical Tsuchimura
endpoint is source-reached, the harness stops with
`K26_FULL_RUN_BUNDLE_BLOCKED_TARGET_NOT_REACHED`. If the endpoint is reached
but no cert is supplied, it stops with
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING`. If the continuation
finishes with live source carry instead of terminal death, the harness stops
with `K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_STILL_LIVE` because there is no
source-dead gap to certify. The gap artifact binds the continuation artifact
and records the exact remaining certificate obligations: bridge safety,
target/coordinate path, terminal inventory, and repaired K26 BZ schedule
evidence. For chunked runs it also binds `k26-continuation-chunks.jsonl` by
SHA-256, so the gap report carries the checked continuation ledger identity.
The bridge obligation can pass as diagnostic evidence when every
source-connected carry atom either bridges, has no legal next-band candidate,
or has only dead-end candidates; the target/coordinate path obligation remains
blocked until the canonical endpoint is positively reached and represented by a
coordinate Gaussian-prime source path from the origin prefix; the terminal
inventory obligation remains blocked until the summary digest/count/max-norm
evidence is promoted to claim-grade inventory provenance. The BZ digest is
bound as accepted-for-schedule but not accepted-for-claim evidence. When
invoked with
`--source-dead-gap-checker`, the harness verifies this gap artifact with the
independent `source_dead_gap_check` before reporting the missing cert blocker.
The prefix-progress JSONL rows are operational telemetry only: they expose
band radii, generated atom counts, edge counts, source carry/death state, and
phase timings so a paid run can be stopped with evidence instead of guesswork.
The continuation-progress JSONL rows do the same for TileOp-port continuation
bands, including tile counts, port graph counts, overflow totals, seam bridge
counts, source carry/death state, and timings. They also include flushed phase
rows before and after expensive stages such as `prefix_witness_read`,
`tileop_build`, `manifest_bridge`, and `source_process`, so a timeout before
the first completed continuation band still leaves evidence for the active
runtime phase. The current local K26 two-band probe completes
`8192 -> 16384` in about 31 seconds and `16384 -> 24576` in about 16 seconds
with 12 local worker threads after enabling overhanging TileOp port support
carry. It reports `terminal_source_dead=false`, `has_source_carry=true`,
`source_carry_atoms=2337`, `source_inventory_count=2107474`, and
`max_source_norm_sq=621084672` at `R=24576`. This corrected an earlier false
terminal diagnostic at `R=16384`; the old `SOURCE_DEAD_CERT_TARGET_NOT_REACHED`
shape remains verifier-covered, but it is not the current local continuation
result. The runner now supports checkpointable TileOp-port continuation:
`--stop-after-bands N` can write a live port carry manifest at the actual
processed boundary, and a later run can resume from that port manifest without
redoing the origin-prefix coordinate bridge. A 2026-05-23 K26-scale resume
probe verified that the uninterrupted two-band run through `R=24576` and a
one-band checkpoint at `R=16384` followed by a resumed second band produce
byte-identical final carry manifests, with `source_carry_atoms=2337` and
`source_inventory_count=2107474`. The sidecar runner parallelizes this TileOp
build loop with standard C++ worker threads, while preserving output order and
reporting
`tileop_worker_threads`; `--tileop-threads N` can pin the count, while
`--tileop-threads 0` uses hardware auto. This is a sidecar execution
optimization and does not change production TileOp semantics. Neither progress
artifact nor live-source continuation evidence relaxes the `SOURCE_DEAD_CERT`
gate.
The bundle harness exposes the same resumability with
`--continuation-chunk-bands N`: it splits the repaired continuation schedule
into bounded chunks, uses the origin-prefix coordinate manifest only for the
first chunk, resumes later chunks from TileOp-port carry manifests,
concatenates chunk progress into `k26-continuation-progress.jsonl`, and copies
the final chunk output to `k26-continuation-result.json` for the existing
bundle checker. It also writes `k26-continuation-chunks.jsonl`, one ledger row
per chunk, recording the schedule slice, input/output carry manifest names,
result/progress artifacts, whether the chunk was executed or reused, and the
observed live/dead source state. Chunk artifacts and the chunk ledger are also
hashed in `k26-full-run-artifacts.sha256`. This is an execution-resilience
protocol, not a certificate relaxation. The full bundle checker validates the
ledger when present: chunk rows must cover the command schedule contiguously,
chain input/output carry manifests, bind every chunk artifact through the hash
manifest, and make the final chunk result byte-identical to
`k26-continuation-result.json`.
If a paid attempt times out after producing complete prefix or chunk artifacts,
rerun the same command with `--resume-existing`. The harness skips the prefix
only when `k26-prefix-result.json`, `k26-prefix-progress.jsonl`,
`k26-prefix-manifest.txt`, and `k26-prefix-witness.txt` are all present. It
skips a non-final continuation chunk only when the chunk JSON/progress/manifest
are present and the chunk result proves live source carry remains for resume.
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
  full repaired schedule has not completed and the endpoint has not been bound
  to a coordinate Gaussian-prime source path.
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
continuation schedule. Its bundle-harness block also names
`k26-continuation-chunks.jsonl` as required acceptance evidence for checked
chunked runs, matching the full bundle checker's ledger validation. The row-0
prefix command targets the canonical-octant
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
same prefix artifacts now completes in about `31s` and keeps source carry live
after the TileOp overhanging-support carry fix. Its row-0 seam bridge counters
remain:
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
`visible coordinate component has no encoded face ports`. A two-band probe
through `24576` then reports `terminal_source_dead=false`,
`has_source_carry=true`, and `source_carry_atoms=2337`. This is useful
bridge-safety and continuation evidence, but it is still not a certificate
because the full repaired schedule, coordinate endpoint path, and claim-grade
terminal inventory are missing.

A bounded local bundle harness run with `--timeout-seconds 120` now stops with
`K26_FULL_RUN_BUNDLE_BLOCKED_K26_CONTINUATION_TIMEOUT` while building band
`40960 -> 49152`; by then bands through `32768 -> 40960` have completed with
live source carry. The current runner and bundle harness can checkpoint and
resume TileOp-port separator state, but no full repaired K26 chunked run has
completed under the active runtime/budget gate yet. The current local blocker
is therefore runtime/budget, not the earlier target-not-reached terminal
diagnostic.

## Stop Conditions

Stop and report without claiming reproduction if:

- source state depends on transient TileOp group labels or union-find roots;
- any overflow appears in a source/origin row;
- coordinate-to-port seam bridging leaves source-connected unbridged carry
  atoms with legal next-band candidates;
- BZ evidence is missing or mismatched for `K=26`;
- terminal inventory cannot preserve retired source components;
- remote runtime or memory threatens the agreed budget.
