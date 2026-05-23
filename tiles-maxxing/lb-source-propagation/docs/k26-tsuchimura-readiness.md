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
  inventory, and a `summary_only_non_claim` mode that still validates the
  positive Gaussian-prime source path and negative guard but treats the
  terminal inventory as a Tsuchimura-scale accumulator shape plus extrema
  witnesses. It emits `SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM_PASS` only for
  that narrower non-claim case. The latter is the scalable K26 contract shape,
  not a certificate acceptance; the real K26 chain, accumulator provenance,
  and full-run BZ digest binding are still missing.

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
live source carry. Paid attempts should additionally pass
`--max-runtime-seconds 14000`, which stops the whole bundle with a
`K26_FULL_RUN_BUNDLE_BLOCKED_*_RUNTIME_LIMIT` status before the `$1.50` budget
can be exhausted. The current runner and bundle harness can checkpoint and
resume TileOp-port separator state, but no full repaired K26 chunked run has
completed under the active runtime/budget gate yet. The current local blocker
is therefore runtime/budget, not the earlier target-not-reached terminal
diagnostic.

A longer 2026-05-23 local resume probe with `--continuation-chunk-bands 8`,
`--timeout-seconds 1800`, and `--max-runtime-seconds 14000` reused the prefix
and completed two continuation chunks: `8192 -> 73728` and
`73728 -> 139264`. The chunk ledger recorded both handoffs, source carry was
still live, the target was not yet seen, and all completed rows had
`tileop_overflows_total=0`. The combined progress through 16 continuation
bands emitted `K26_RUNTIME_BUDGET_PASS` with projected total `7684s`, but the
next chunk's first completed band `139264 -> 147456` took `152.237s` and the
chunk-local runtime checker emitted `K26_RUNTIME_BUDGET_REJECT` with projected
total `18726s`. The local run was stopped on that budget signal. This does not
invalidate the source-propagation protocol or checkpoint chain, but it means a
full local K26 continuation is not accepted under the current runtime gate; a
paid or larger-machine attempt must keep the same runtime checker active and
stop if the later-radius projection remains over budget.

A bounded 2026-05-23 Vast RTX 4090 timing probe at commit `3483137` ran under
the price gate on instance `37436093` at about `$0.355/hr`, pulled artifacts to
`tiles-maxxing/lb-source-propagation/artifacts/vast-k26-timing-pull`, and
destroyed the instance after exit. The probe built the K26 sidecar but timed
out inside `K26_CONTINUATION_CHUNK_000` with
`K26_FULL_RUN_BUNDLE_BLOCKED_K26_CONTINUATION_CHUNK_000_TIMEOUT`. It completed
six continuation bands through `R=57344`, all with
`tileop_overflows_total=0`, live source carry, and target not seen. The
tail-conservative runtime checker rejected the run:
`completed_band_count=6`, cumulative projection `20575s`, latest-band tail
projection `28229s`, effective projection `28229s`, and budget margin
`-14229s` against the `14000s` cap. This confirms the current cheap 4090 Vast
shape is not a viable full K26 CPU TileOp continuation path; it is timing
evidence only, not a `SOURCE_DEAD_CERT`, not a sqrt(26) reproduction, and not a
moat result.

A follow-up bounded Vast RTX 4090 timing probe at deployed local head
`984d2f1` pinned `--tileop-threads 6` to test whether the previous run was hurt
by using all 32 visible hardware threads on a fractional-CPU host. The probe
ran on instance `37439137`, pulled artifacts to
`tiles-maxxing/lb-source-propagation/artifacts/vast-k26-timing-pull-t6`, and
destroyed the instance after exit. It completed chunk `000`
(`8192 -> 73728`) with live source carry (`source_carry_atoms=8362`),
`tileop_overflows_total=0`, and target not seen, then was stopped in chunk
`001` after the budget rejection was already clear. Pinning helped the later
chunk-0 bands compared with the 32-thread auto run, for example
`40960 -> 49152` improved from `195.776s` to `118.120s`, but it still did not
meet the full-run budget gate. The manual runtime checker over the completed
chunk emitted `K26_RUNTIME_BUDGET_REJECT` with `completed_band_count=8`,
cumulative projection `14891s`, latest-band tail projection `23084s`,
effective projection `23084s`, and budget margin `-9084s`. This means thread
pinning is useful execution control, but this cheap 4090/fractional-CPU host
class still cannot justify a full K26 continuation under the active cap.

A final one-band remote timing probe at deployed local head `9d4010f` validated
the safer timing-probe default. It ran with `--chunk-bands 1` and
`--tileop-threads 6` on instance `37442799`, pulled artifacts to
`tiles-maxxing/lb-source-propagation/artifacts/vast-k26-timing-pull-t6-c1`,
and destroyed the instance after exit. The harness stopped immediately after
chunk `000` (`8192 -> 16384`) with
`K26_FULL_RUN_BUNDLE_BLOCKED_K26_CONTINUATION_CHUNK_000_RUNTIME_BUDGET_REJECT`.
The single completed band had `tileop_overflows_total=0`, live source carry
(`source_carry_atoms=1437`), and target not seen. The runtime checker reported
`completed_band_count=1`, effective/cumulative/tail projection `15151s`, and
budget margin `-1151s`. This confirms that one-band remote timing probes can
reject this host shape in about one continuation band instead of burning a
whole eight-band chunk.

After that rejection, the first sidecar-only runtime fixes target the measured
`manifest_bridge` and `source_process` hotspots without changing TileOp bytes,
CUDA kernels, certificate semantics, or final sorted bridge output. The
coordinate carry lookup inside `source_tileop_port_runner` now uses a reserved
hash table keyed by stable `(a,b)` coordinates and precomputes the finite
K-neighborhood offsets once per bridge. The source-propagation inventory merge
now sorts only new per-band atoms and linearly merges already-canonical
incoming component inventories, instead of re-sorting the full accumulated
source inventory on every continuation band. Direct API inputs with unsorted
incoming inventory remain accepted and canonicalized. Local verification after
the changes: full sidecar CTest passed `28/28`, the Phase 1 diff-scope guard
passed, a matching-K36 progress smoke emitted `manifest_bridge` and
`source_process` phase rows, and a local K26-compiled first-continuation probe
over `8192 -> 16384` with `--tileop-threads 6` completed with live source
carry in `6.719s` total: `tileop_ms=3516`, `manifest_bridge=2085ms`, and
`source_process=1104ms`. It preserved the row-0 bridge-safety counters,
including `source_unbridged_unsafe_candidate_atoms=0`. This is runtime
evidence only; it does not yet prove that the full K26 continuation fits the
paid budget gate, so another paid timing probe still requires the same one-band
default, price cap, and runtime checker.

A bounded local continuation probe at optimized head `6a20e1a` then measured
the first 12 K26 continuation bands. The first chunk `8192 -> 73728` completed
8 bands in `92.927s`, kept source carry live with `source_carry_atoms=8362`,
had `tileop_overflows_total=0`, did not see the target, and emitted
`K26_RUNTIME_BUDGET_PASS`: cumulative projection `1429s`, latest-band tail
projection `2547s`, effective projection `2547s`, and budget margin `+11453s`.
A second bounded continuation from the emitted port manifest covered
`73728 -> 106496` in 4 more bands. Across all 12 completed bands, the default
tail-conservative runtime checker still emitted `K26_RUNTIME_BUDGET_PASS`:
observed continuation time `203.590s`, cumulative projection `2087s`,
latest-band tail projection `3960s`, effective projection `3960s`, and budget
margin `+10040s`. A four-band tail window over the same combined progress
projected `3403s`. This is local non-claim timing evidence only, but it
reopens a capped one-band Vast timing probe as meaningful once an RTX 4090
offer satisfies the `$0.37/hr` gate.

The same audit also exposed the remaining terminal-inventory scale blocker.
The current Phase 1 separator keeps explicit `component_inventory` vectors so
small stitched-vs-big proofs and terminal-death checks can compare exact atom
sets. That is not a claim-grade representation for Tsuchimura scale: the
expected terminal source component has `14,542,615,005` members. The sidecar
therefore now treats `--max-atoms` as a cap on accumulated component inventory
as well as current band atoms, carry atoms, and components. If the explicit
inventory exceeds the cap, `process_band` rejects with
`reject=overflow` and `reject_diagnostic="component inventory exceeds source cap"`.
A K26 first-continuation probe with an intentionally low `--max-atoms 2000000`
confirmed the deterministic blocker on `8192 -> 16384`. This prevents a long
run from exhausting memory silently, but it also means a full Tsuchimura-grade
`SOURCE_DEAD_CERT` still needs a streaming or accumulator-based terminal
inventory witness before the 14.5B-member comparison can be accepted.

A guarded paid retry was attempted after this optimization at local head
`6f94b31`, but no RTX 4090 offer satisfied the active `$0.37/hr` cap. The
guard refused to rent; the nearest observed qualifying-market offer was about
`$0.4000/hr`, `0.0300/hr` over cap. No Vast instance was created.

After the source-inventory merge optimization at local head `3bb5fa5`, the
same guarded one-band timing retry was attempted with a 120-second offer wait.
No RTX 4090 offer satisfied the active `$0.37/hr` cap; the nearest observed
qualifying-market offer was again about `$0.4000/hr`, `0.0300/hr` over cap.
No Vast instance was created.

After recording the 12-band local timing pass at local head `7996d30`, the
guarded one-band Vast retry was attempted again with a 120-second offer wait.
No RTX 4090 offer satisfied the active `$0.37/hr` cap; the nearest observed
qualifying-market offer was about `$0.4000/hr`, `0.0300/hr` over cap. No Vast
instance was created.

After binding the explicit non-claim terminal accumulator contract at local
head `7d1098a`, a dry-run Vast offer probe was attempted with the same
`$0.37/hr` and `$1.50` caps. No RTX 4090 offer satisfied the cap; the nearest
observed qualifying-market offer was `$0.6685/hr`, `0.2985/hr` over cap. No
Vast instance was created.

After requiring the K26 harness to copy the terminal accumulator from the
continuation artifact at local head `157a9df`, a 120-second dry-run Vast offer
probe was attempted with the same `$0.37/hr` and `$1.50` caps. No RTX 4090
offer satisfied the cap; the nearest observed qualifying-market offer was
`$0.6685/hr`, `0.2985/hr` over cap. No Vast instance was created.

After binding the mixed target atom-chain start to the prefix witness at local
head `8a6363c`, another 120-second dry-run Vast offer probe used the same
`$0.37/hr` and `$1.50` caps. No RTX 4090 offer satisfied the cap; the nearest
observed qualifying-market offer was `$0.4010/hr`, `0.0310/hr` over cap. No
Vast instance was created.

After binding the K26 gap to explicit target-bridge evidence at local head
`4c6e867`, another 120-second dry-run Vast offer probe used the same
`$0.37/hr` and `$1.50` caps. No RTX 4090 offer satisfied the cap; the nearest
observed qualifying-market offer was `$0.4000/hr`, `0.0300/hr` over cap. No
Vast instance was created.

After binding the K26 gap to explicit BZ schedule obligations at local head
`b50375c`, a capped Vast RTX 4090 offer appeared at `$0.3481/hr`, inside the
`$0.37/hr` cap. Instance `37454361` ran the guarded one-shot remote smoke on
2026-05-23, then `--destroy-on-exit` destroyed it. Pulled artifacts under
`tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-pull/` record
`REMOTE_SIDECAR_SMOKE_PASS`, sidecar CTest `28/28`, independent verification
CTest `84/84`, and `REMOTE_SIDECAR_SMOKE_ARTIFACTS_PASS`. This remains a
build/smoke and small/medium verification gate only, not a sqrt(26) source run
and not a moat result.

After binding the K26 gap to origin-prefix path provenance at local head
`da765d6`, a dry-run saw an RTX 4090 offer at `$0.3587/hr`, but the guarded
execute path rechecked the market before creating and found no qualifying offer.
The nearest observed qualifying-market offer was `$0.5081/hr`, `0.1381/hr`
over cap. No Vast instance was created.

After binding the K26 mixed coordinate/TileOp-port atom chain to local
per-port expansion evidence at local head `e3fd90f`, a capped dry-run Vast
offer probe used the same `$0.37/hr` and `$1.50` caps. No RTX 4090 offer
satisfied the cap; the nearest observed qualifying-market offer was
`$0.6681/hr`, `0.2981/hr` over cap. No Vast instance was created.

After binding full coordinate-port expansion paths and summary-only
non-claim cert generation at local head `6f423ff`, a capped dry-run saw an
RTX 4090 offer at `$0.2947/hr`, inside the `$0.37/hr` cap. The guarded execute
path rechecked the market before creating an instance and found no qualifying
offer. The nearest observed qualifying-market offer was `$0.4014/hr`,
`0.0314/hr` over cap. No Vast instance was created; `vastai show instances
--raw` returned `[]` after the attempt.

After exposing the auto summary/non-claim certificate contract at local head
`a5dd815`, a guarded Vast smoke attempt found RTX 4090 offer `31475019` on
host `53663` at `$0.2801/hr`, inside the cap, and created instance
`37460590`. SSH metadata never became available within the 600-second readiness
window, so no deploy or remote smoke ran. Because the attempt used
`--destroy-on-exit`, the guard destroyed the unready instance. A final
`vastai show instances --raw` returned `[]`.

After recording the SSH-timeout attempt at local head `73c6417`, a guarded
retry excluded host `53663` and enabled
`tiles-maxxing/lb-source-propagation/artifacts/vast-k26-failures.ledger`.
It selected RTX 4090 offer `28429701` on host `1647` at `$0.3347/hr`, inside
the cap, created instance `37461974`, waited for SSH readiness, deployed the
current tree, and ran the one-shot remote smoke. The smoke passed and
`--destroy-on-exit` destroyed the instance. Pulled artifacts under
`tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-pull/` record
`deployed_local_head=73c6417`, `REMOTE_SIDECAR_SMOKE_PASS`, sidecar CTest
`28/28`, independent verification CTest `86/86`, and
`REMOTE_SIDECAR_SMOKE_ARTIFACTS_PASS`. A final `vastai show instances --raw`
returned `[]`. This satisfies the Phase 1 remote build/smoke gate for the
auto-summary certificate contract, but remains non-claim: it is not a
sqrt(26) source/origin run and not a moat result.

After recording the successful remote smoke at local head `bbc20d7`, a guarded
one-band K26 timing-probe attempt used the same cap and excluded host `53663`.
No RTX 4090 offer satisfied `$0.37/hr`; the nearest observed
qualifying-market offer was `$0.4000/hr`, `0.0300/hr` over cap. No Vast
instance was created, the failure ledger recorded the capped stop, and
`vastai show instances --raw` returned `[]`.

After binding the remote K26 timing probe to independently built source-death
checkers at local head `a66d021`, another guarded one-band timing-probe
attempt used the same `$0.37/hr` and `$1.50` caps and excluded host `53663`.
No RTX 4090 offer satisfied the cap; the nearest observed qualifying-market
offer was `$0.5214/hr`, `0.1514/hr` over cap. No Vast instance was created,
the failure ledger recorded the capped stop, and `vastai show instances --raw`
returned `[]`.

After any partial continuation, run:

```bash
check_k26_runtime_budget.py \
  --progress OUT_DIR/k26-continuation-progress.jsonl \
  --chunk-ledger OUT_DIR/k26-continuation-chunks.jsonl \
  --schedule-segment-count 123 \
  --max-runtime-seconds 14000
```

This emits `K26_RUNTIME_BUDGET_PASS`, `K26_RUNTIME_BUDGET_REJECT`, or
`K26_RUNTIME_BUDGET_INSUFFICIENT_PROGRESS` as diagnostic non-claim evidence.
By default it computes both a cumulative-average projection and a conservative
tail projection from the latest completed band; the effective
`projected_total_seconds` is the larger of those values. This prevents early
cheap bands from masking later-radius runtime growth. It is a stop/continue
guard for budgeted execution, not source/origin proof.
For chunked continuation, the checker treats radial intervals as the stable
identity of completed rows; local `band_index` values may restart inside each
resumed `source_tileop_port_runner` process.
The full bundle harness also invokes this checker automatically after a
continuation timeout, whole-bundle runtime-limit stop, failed continuation with
progress, or completed continuation. The harness stores the raw checker output
as `k26-runtime-budget-check.log` plus stderr/meta sidecars, and copies the
checker status, effective projection, cumulative projection, tail projection,
tail window, margin, last completed radius, progress artifact, and exit code
into `status.txt` when those fields exist. A paid attempt should therefore
leave enough runtime evidence to decide whether to resume, reduce chunk size,
or stop under the `$1.50` cap without weakening the `SOURCE_DEAD_CERT` gate.
For chunked continuation, the harness now enforces the same checker after each
completed chunk against the cumulative appended progress. If the projection is
already over budget, it stops with
`K26_FULL_RUN_BUNDLE_BLOCKED_K26_CONTINUATION_CHUNK_<id>_RUNTIME_BUDGET_REJECT`
before launching the next chunk.
Remote timing probes therefore default to `--chunk-bands 1`, while the full
bundle command contract can still use larger chunks such as `8` for resumable
execution when the runtime profile is already known to be acceptable.
The remote timing probe also builds the independent `source_dead_gap_check`
and `source_dead_cert_check` verifier binaries in its own verification build
directory, then passes both paths into the bundle harness. That means a paid
one-band, resumed, terminal, or target-reaching probe exercises the same
auto-generated `SUMMARY_ONLY_NON_CLAIM` source-death summary contract as the
local checked bundle path, instead of stopping at the older unchecked
missing-certificate blocker. This still remains diagnostic unless a future
artifact satisfies the full `SOURCE_DEAD_CERT` checker.

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
