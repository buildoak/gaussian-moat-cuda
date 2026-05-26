# sqrt(26) Tsuchimura Readiness

This note is the execution guard for the first source/origin comparison target.
It is not a result claim and it is not a first-read LB architecture document.
Read it only when touching K26. Remote budget values are conditional execution
guardrails, not standing authorization for paid compute.

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
`k26-continuation-result.json`, if K26 cert metadata does not bind the exact
repaired BZ schedule digest
`sha256:lb_source_k26_repaired_bz_schedule_v1:7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95`,
or if the independent
`source_dead_cert_check` does not accept a listed source-dead draft. The
independent cert checker also rejects a cert whose endpoint still lies in the
conservative terminal guard shell
`R_final - ceil(sqrt(K)) <= |p| <= R_final`; this makes
`negative_guard_pass` a checked geometric condition, not just a boolean field.
The `k26-source-dead-gap.json` layer also machine-checks explicit obligation
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
useful diagnostic shape evidence only when it also carries an explicit
`terminal_source_inventory_accumulator` with
`mode=summary_digest_only_non_claim` and
`claim_grade_inventory_accepted=false`; the bundle checker reports
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM` instead
of accepting the bundle.

The executable run contract now separates two checked harness paths. Without
`--cert-in`, supplying both `--source-dead-gap-checker` and
`--source-dead-checker` asks the harness to generate
`k26-source-dead-cert.json` as a summary-only non-claim artifact from the
prefix witness plus full coordinate-port expansion paths, then run the bundle
checker. With `--cert-in`, the supplied cert is copied and checked instead.
Both paths remain non-claim unless the cert checker returns a listed
`SOURCE_DEAD_CERT_DRAFT_PASS` and the bundle checker accepts every binding.

The executable harness for producing the bundle shape is:

```bash
run_k26_full_source_bundle.sh \
  --build-dir BUILD_DIR \
  --out-dir OUT_DIR \
  --continuation-chunk-bands 8 \
  --resume-existing \
  --timeout-seconds 1200 \
  --max-runtime-seconds 14000
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
and both independent source-dead checkers are supplied, the harness now
synthesizes `k26-source-dead-cert.json` as a
`SUMMARY_ONLY_NON_CLAIM` artifact from the row-0 prefix witness plus the full
coordinate-port expansion paths in the continuation JSON, then lets the bundle
checker report
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM`. If the
endpoint is reached but no cert or cert checkers are supplied, it still stops
with `K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING`. If the
continuation finishes with live source carry instead of terminal death, the
harness stops with `K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_STILL_LIVE` because
there is no source-dead gap to certify. The gap artifact binds the continuation artifact
plus the prefix manifest and prefix witness, and records the exact remaining
certificate obligations: bridge safety, target/coordinate path, terminal
inventory, and repaired K26 BZ schedule evidence. `bz_schedule_obligation`
binds the repaired K26 BZ digest as schedule-accepted but source-claim
non-accepted, which keeps non-square BZ evidence out of `SOURCE_DEAD_CERT`
until the cert layer has a claim-grade BZ gate. When the mixed target atom chain
is present, the gap also requires the first coordinate atom to be a target row
in `k26-prefix-witness.txt`; the origin-prefix side is then bound. The
continuation now preserves a compact `prefix_witness_path` summary for that
first coordinate atom plus a `coordinate_port_expansions` object for every
coordinate/port edge in the mixed atom chain. Each expansion includes the full
local Gaussian-prime path between the coordinate atom and the representative
TileOp-port witness. The gap binds the counts into
`coordinate_path_obligation`, and the harness uses the full expansion paths to
assemble the summary-only positive source path. This is still non-claim until
a claim-grade verifier binds the full coordinate path, terminal inventory, and
BZ schedule.
The same gap records a `target_bridge_obligation`
that mirrors whether the canonical endpoint was seen, bridged to TileOp ports,
and source-reached. For chunked runs it also binds
`k26-continuation-chunks.jsonl` by SHA-256, so the gap report carries the
checked continuation ledger identity. It separately binds
`k26-continuation-chunk-000.json` when chunking is used, because
coordinate-to-port bridge safety is a row-0 seam obligation while the
terminal/path/inventory fields are final-continuation obligations.
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
`--tileop-threads 0` uses hardware auto. The K26 bundle harness and remote
timing probe now pass this option through to `source_tileop_port_runner`, so a
future paid timing probe can test fractional-CPU Vast hosts with a pinned
worker count instead of using every visible hardware thread. This is a sidecar
execution optimization and does not change production TileOp semantics.
Neither progress artifact nor live-source continuation evidence relaxes the
`SOURCE_DEAD_CERT` gate.
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
`SOURCE_DEAD_CERT` logic. `--timeout-seconds` should be set for paid runs as a
per-command kill switch, and `--max-runtime-seconds` should be set as the
whole-bundle wall-clock budget guard. At the campaign cap of `$0.37/hr` and
`$1.50` total, `14000` seconds leaves a small shutdown margin while preserving
resume artifacts. If the supplied cert is only a summary accumulator non-claim,
the harness writes
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM` to
`status.txt` and exits nonzero.

## Executable Contract

The executable contract is intentionally narrow:

```bash
k26_source_run_contract
k26_execution_plan
k26_bz_schedule_check
k26_source_run_profile
k26_source_run_commands
```

These programs emit machine-checkable non-claim contracts for the Tsuchimura
comparison target. They must keep `executable_now=false` and must not emit
`SOURCE_DEAD_CERT_PASS`, `MOAT_PROOF_PASS`, or `SPAN_PROOF_PASS` until a future
claim-grade verifier accepts the full source/death certificate.

The K26 path is blocked until all of the following are true:

- the repaired variable-boundary schedule completes under an explicitly
  authorized run budget;
- coordinate-to-port bridge safety is accepted by verifier evidence;
- terminal inventory is represented by a claim-grade accumulator, not a flat
  historical list;
- a coordinate Gaussian-prime path binds the canonical endpoint to the certified
  source;
- BZ/schedule evidence, commit/build identity, artifact hashes, path evidence,
  and terminal inventory are bound into one accepted `SOURCE_DEAD_CERT`.

Remote smoke, timing probes, runtime-budget checks, and historical Vast offer
ledgers are operational diagnostics. They are not readiness canon and should be
read from git history or pulled artifacts only when investigating a specific
run.

After any partial continuation, `check_k26_runtime_budget.py` may be used as a
stop/continue budget guard over `k26-continuation-progress.jsonl` and
`k26-continuation-chunks.jsonl`. Its statuses are diagnostic non-claim evidence.

## Stop Conditions

Stop and report without claiming reproduction if:

- source state depends on transient TileOp group labels or union-find roots;
- any overflow appears in a source/origin row;
- coordinate-to-port seam bridging leaves source-connected unbridged carry
  atoms with legal next-band candidates;
- BZ evidence is missing or mismatched for `K=26`;
- terminal inventory cannot preserve retired source components; the current
  explicit-inventory sidecar must stop with `component inventory exceeds source
  cap` rather than attempting a Tsuchimura-scale flat inventory;
- remote runtime or memory threatens the agreed budget.
