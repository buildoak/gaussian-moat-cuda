# LB Source Propagation Sidecar

This is the Phase 1 CPU sidecar for lower-bound source propagation. It is not a
replacement for the existing TileOp/CUDA campaign; it is the source-stitching
protocol plus smoke contact with the existing CPU TileOp producer.

The proof model is documented in
`methodology/source-propagation-band-stitching.md`.

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
no first-band port bridge are split into "no legal next-band candidate" and
"candidate existed but no accepted bridge" counters, with source-only versions
of the same counters. This is still diagnostic: the seam bridge is explicit
evidence for the next engineering gate, not an accepted source/death
certificate.
The runner also has `--require-full-bridge`, which rejects a manifest handoff
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
tiles-maxxing/lb-source-propagation/scripts/check_phase1_local_gates.sh
```

This runs fresh local sidecar CMake/CTest, fresh independent `verification/`
CMake/CTest, `git diff --check`, the Phase 1 diff-scope guard, and the same
artifact checker used after a pulled Vast smoke. To run only the branch-scope
gate:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_phase1_diff_scope.sh
```

The tests cover five- and ten-band composed runs versus one big band, false
welding, source-only-carry loss, neutral partition merges, terminal inventory,
hard overflow rejection, `K=32` carry width, associativity across band
groupings, certified source seed application/rejection, carry-manifest
round-trip/rejection, exact draft JSON output, CPU TileOp producer smoke, and
sqrt(26) Tsuchimura preflight/run-contract constants.

## Remote Smoke

Find a qualifying Vast 4090 without renting:

```bash
tiles-maxxing/lb-source-propagation/scripts/vast_sidecar_smoke_guard.sh \
  --max-dph 0.37 \
  --max-budget 1.50 \
  --k-sq 26
```

To create only when the cap is satisfied, and stop the new instance if SSH
never opens:

```bash
tiles-maxxing/lb-source-propagation/scripts/vast_sidecar_smoke_guard.sh \
  --execute \
  --max-dph 0.37 \
  --max-budget 1.50 \
  --k-sq 26 \
  --offer-wait-seconds 900 \
  --offer-poll-seconds 30 \
  --wait-ssh-seconds 300 \
  --stop-on-ssh-timeout
```

To run the whole paid smoke gate as one bounded command, add the explicit
remote-smoke and cleanup switches:

```bash
tiles-maxxing/lb-source-propagation/scripts/vast_sidecar_smoke_guard.sh \
  --execute \
  --run-remote-smoke \
  --destroy-on-exit \
  --max-dph 0.37 \
  --max-budget 1.50 \
  --k-sq 26 \
  --offer-wait-seconds 900 \
  --offer-poll-seconds 30 \
  --failure-ledger tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-failures.tsv \
  --max-create-attempts 3 \
  --wait-ssh-seconds 600 \
  --ssh-poll-seconds 10
```

This creates the instance only after the price cap passes, waits for SSH,
deploys the current tree, runs `remote_sidecar_smoke.sh`, pulls artifacts into
`tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-pull`, runs
`check_remote_smoke_artifacts.sh --expect-head --expect-branch --expect-k-sq`,
and destroys the created instance on exit.
When `--max-create-attempts` is greater than `1`, a timed-out SSH probe destroys
the unready instance, excludes that offer id, and tries the next capped offer.
With `--failure-ledger`, the guard also loads prior failed `offer_id`/`host_id`
rows as exclusions and appends new create/SSH failures for future attempts.
When no capped offer exists, the guard performs a second no-rent market scan
without the `dph <= 0.37` predicate and records the nearest RTX 4090 offer in
both stdout and the failure ledger. This distinguishes an empty market from an
over-cap market while preserving the no-rental budget rule.

If a Vast offer or host repeatedly creates an instance whose advertised SSH
port never opens, exclude it on the next attempt:

```bash
tiles-maxxing/lb-source-propagation/scripts/vast_sidecar_smoke_guard.sh \
  --max-dph 0.37 \
  --max-budget 1.50 \
  --k-sq 26 \
  --exclude-offer-id 30257785 \
  --exclude-host-id 53663
```

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

The full-run bundle gate is:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_k26_full_run_bundle.sh \
  /path/to/k26-full-run-artifacts \
  --source-dead-checker /path/to/source_dead_cert_check \
  --source-dead-gap-checker /path/to/source_dead_gap_check
```

It is intentionally stricter than the remote smoke checker. It expects the
K26 prefix result, diagnostic-bridge TileOp-port continuation result, BZ
schedule evidence, run profile, run command contract,
`k26-source-dead-gap.json`, and `k26-source-dead-cert.json`; it rejects digest
mismatches, malformed gap artifacts, unsafe source-connected bridge gaps,
overflow, wrong component size, missing source-dead draft, a source-dead draft
whose terminal inventory summary does not match the executed continuation
result, or a source-dead draft not accepted by the independent checker.
When a chunk ledger is present, the checker also validates that the ledger
covers the command schedule contiguously, chains carry manifests, binds every
chunk artifact through the hash manifest, and matches the final chunk result to
`k26-continuation-result.json`.

The paid/full-run harness is:

```bash
tiles-maxxing/lb-source-propagation/scripts/run_k26_full_source_bundle.sh \
  --build-dir /tmp/gm-lbsp-remote-smoke \
  --out-dir /workspace/k26-full-source-bundle \
  --continuation-chunk-bands 8 \
  --resume-existing \
  --timeout-seconds 1200 \
  --max-runtime-seconds 14000 \
  --source-dead-gap-checker /path/to/source_dead_gap_check
```

It runs the exact K26 command/profile/BZ emitters, then executes the row-0
coordinate prefix and diagnostic-bridge TileOp-port continuation. It does not
manufacture a source-dead certificate. If terminal source death occurs but the
canonical Tsuchimura endpoint was not source-reached, it writes
`K26_FULL_RUN_BUNDLE_BLOCKED_TARGET_NOT_REACHED` to `status.txt`; that is a
terminal diagnostic blocker, not a missing-cert case. If the endpoint was
reached but no `k26-source-dead-cert.json` is available, it writes
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_MISSING` to `status.txt` after
the prefix and continuation artifacts are produced. It also writes
`k26-prefix-progress.jsonl`, one JSON row per processed coordinate-prefix band,
with atom/edge counts, source carry/death state, and timing fields. This is
paid-run observability, not claim evidence. The harness also writes
`k26-continuation-progress.jsonl`, one JSON row per processed TileOp-port band,
with tile counts, port atom/edge counts, overflow totals, seam bridge counts,
source carry/death state, and timing fields. This is also observability only.
The harness also writes
`k26-source-dead-gap.json`, binding the continuation artifact and naming the
remaining certificate obligations. For chunked runs, the gap also binds
`k26-continuation-chunks.jsonl` by SHA-256 so the certificate-gap report points
to the same ledger that the bundle checker validates. In chunked runs it also
binds `k26-continuation-chunk-000.json` as the bridge-source artifact, because
row-0 coordinate-to-port bridge safety is observed at the first continuation
chunk while terminal/path/inventory evidence is observed at the final chunk.
`bridge_safety` records the
`source_unbridged_unsafe_candidate_atoms == 0` bridge condition.
`coordinate_path_obligation` records why the observed target evidence is not a
coordinate Gaussian-prime source path. For a reached diagnostic target, the
blocker is the mixed coordinate/TileOp-port atom chain. For a terminal
diagnostic where the target was not reached, the blocker is
`SOURCE_DEAD_CERT_TARGET_NOT_REACHED`. If source carry is still live at the
requested terminal radius, the harness stops earlier with
`K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_STILL_LIVE` and does not write a
source-dead gap. `terminal_inventory_obligation` records that the observed
inventory is still summary-digest non-claim evidence and must be promoted to
claim-grade terminal inventory provenance. The gap artifact also binds the
repaired K26 BZ schedule digest as schedule-only, non-claim evidence.
When
`--source-dead-gap-checker` is supplied, the harness runs the independent gap
checker before stopping on either a target-not-reached gap or the missing cert,
and mirrors the checker status fields into `status.txt` so remote logs show
whether bridge safety passed and which certificate obligations remain blocked.
It also writes
`k26-full-run-artifacts.sha256`, binding the command, BZ, profile, prefix,
prefix-progress, continuation, continuation-progress, manifest, witness, gap,
and any supplied cert artifacts by SHA-256.
Use `--timeout-seconds` on paid runs as a per-command kill switch, and
`--max-runtime-seconds` as the whole-bundle wall-clock budget guard. At the
campaign cap of `$0.37/hr` and `$1.50` total, `14000` seconds leaves a small
shutdown margin while preserving resume artifacts. `source_tileop_port_runner
--progress-out` writes phase rows before and after expensive continuation
stages (`manifest_read`, `prefix_witness_read`,
`grid_build`, `tileop_build`, `port_graph`, `target_bridge`,
`manifest_bridge`, and `source_process`) as well as completed-band rows. A
timeout can therefore identify the active phase even when no continuation band
has finished yet. The current local K26 two-band probe completes
`8192 -> 16384` in about 31 seconds and `16384 -> 24576` in about 16 seconds on
12 local worker threads after enabling overhanging TileOp port support carry.
It reports `terminal_source_dead=false`, `has_source_carry=true`, and
`source_carry_atoms=2337` at `R=24576`. This corrected an earlier false
terminal diagnostic at `R=16384`; the previous
`SOURCE_DEAD_CERT_TARGET_NOT_REACHED` artifact remains a valid gap shape, but
it is no longer the current local K26 continuation result. The TileOp-port
runner can also checkpoint live continuation state with `--stop-after-bands N`
and resume from the written port carry manifest. A K26-scale probe verified
that full two-band continuation through `R=24576` and a one-band checkpoint at
`R=16384` plus resumed second band produce byte-identical final carry
manifests, with `source_carry_atoms=2337` and `source_inventory_count=2107474`.
The bundle harness exposes this as `--continuation-chunk-bands N`: it splits
the repaired continuation schedule into bounded chunks, concatenates chunk
progress into the canonical continuation progress JSONL, preserves
`k26-continuation-result.json` as the final chunk result, and hashes chunk
artifacts in the bundle manifest. It also writes
`k26-continuation-chunks.jsonl`, one ledger row per chunk, recording the
schedule slice, input/output carry manifest names, result/progress artifacts,
whether the chunk was executed or reused, and the observed live/dead source
state. Add `--resume-existing` after a timeout to reuse complete prefix
artifacts and complete live-source chunks from the same output directory
instead of restarting the paid continuation from row 0.
`check_k26_runtime_budget.py` can be run against
`k26-continuation-progress.jsonl` after any partial continuation. It reads the
completed TileOp-port band timings, projects the full 123-segment continuation
against the `14000` second wall-clock budget, reports the last completed radius
and active phase context, and emits only diagnostic non-claim status. For
chunked runs, completed progress rows are keyed by radial interval rather than
local per-process `band_index`, so appended chunk progress remains valid even
when each resumed runner starts its local band counter at zero.
The bundle harness now runs this checker automatically whenever a continuation
command times out, hits the whole-bundle runtime limit, or completes. It writes
`k26-runtime-budget-check.log`, `k26-runtime-budget-check.err`, and
`k26-runtime-budget-check.meta`, then mirrors the runtime-budget status,
completed-band count, projection, margin, last completed radius, progress
artifact, and checker exit code into `status.txt` when available. This makes a
paid stop/retry decision reproducible without treating runtime evidence as
source/origin proof.
The TileOp build loop is parallelized only inside this sidecar runner with
standard C++ worker threads, preserving the deterministic output order and
reporting
`tileop_worker_threads` in progress/final JSON. Use `--tileop-threads N` to pin
the worker count, or `--tileop-threads 0` for hardware auto. This does not
alter the underlying TileOp implementation or any existing campaign verdict
semantics. Live-source continuation evidence is diagnostic runtime evidence,
not a `SOURCE_ORIGIN_K26` claim.
If a cert is supplied with `--cert-in`, it copies it into the bundle, refreshes
the hash manifest, and runs the full bundle checker with the supplied
`--source-dead-checker`.

The exact non-claim command contract for the eventual K26 run is emitted by:

```bash
k26_source_run_commands
```

It prints the origin-prefix command and the repaired
`source_tileop_port_runner --schedule-radii ...` continuation command. The
output is a run contract only; it uses the canonical-octant representative
`376039 + 943460i` for Tsuchimura's endpoint `943460 + 376039i`, does not
execute the full sqrt(26) comparison, and must not be treated as a
`SOURCE_DEAD_CERT`. The emitted continuation command includes
`--target-a 376039 --target-b 943460` and allows dead-end unbridged carry
diagnostics. The claim-grade bridge condition is:
`source_unbridged_unsafe_candidate_atoms` must be zero, and the canonical
endpoint must be seen and source-reached in the continuation artifact before
the full bundle checker can pass. The TileOp-port target field
now reports `path_provenance=mixed_coordinate_port_atom_chain_non_claim` plus a
stable atom id chain when target reachability is proved. That is stronger than
a boolean component bit, but it is still a mixed coordinate/port graph witness,
not a coordinate Gaussian-prime source path suitable for a `SOURCE_DEAD_CERT`.

`k26_source_run_contract` emits the execution contract for the Tsuchimura
comparison target. It is intentionally a non-claim artifact:
`"executable_now": false` remains correct until the bundle harness has produced
accepted full-run artifacts, terminal inventory provenance is claim-grade, the
full-run K26 BZ digest is bound, and an accepted `SOURCE_DEAD_CERT` artifact
exists.

`k26_execution_plan` emits the machine-checkable execution plan for the same
target. It expands the conservative `R_final=1015645` guard into 124 radial
rows at preferred width 8192, records the final row width 8029, binds the active
Vast budget caps, and uses the BZ-repaired row boundaries emitted by
`k26_bz_schedule_check`. It also carries the repaired BZ schedule digest so the
eventual full-run profile can bind the exact schedule it consumed. This is
still a non-claim artifact and must keep `"executable_now": false` until the
K26 blockers are closed.

`k26_bz_schedule_check` is exact integer evidence for K26 non-square bad-zone
reconciliation. It records that the nominal 124-row, width-8192 schedule is not
BZ-clean: rows `15`, `58`, and `75` contain Gaussian-prime norms in a bad zone.
It then emits a repaired schedule using the nearest clean internal boundary,
choosing negative delta before positive on ties. The current repaired
boundaries shift `122880`, `475136`, and `622592` down by `1`; all repaired
rows are BZ-clean. Its status is `BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE`, with
digest `sha256:lb_source_k26_repaired_bz_schedule_v1:7c820f641cc218631ddc2bc22c5767a70e8608ec4fdb293fadde6cc1fde57b95`.
That accepts the schedule evidence only; it is not `SOURCE_ORIGIN_K26` or
`SOURCE_DEAD_CERT`.

`k26_source_run_profile` binds the repaired schedule to the intended full-run
shape: exact coordinate prefix for row `0`, then TileOp-port continuation for
rows `1..123` from an origin-prefix manifest and witness. It also records the
next concrete implementation gap: the TileOp-port runner can consume explicit
variable boundaries through the bundle harness, and checked chunked execution
must include `k26-continuation-chunks.jsonl` as the validated chunk ledger, but
no accepted full K26 source run has completed that schedule yet.

## Integration Boundary

Phase 2 should feed this protocol from the existing CPU TileOp/band machinery
without changing current static-annulus verdict semantics. Stable carry atoms
must be coordinates or canonical port atoms, not transient union-find roots.
