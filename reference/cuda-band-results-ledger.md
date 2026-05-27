# CUDA Band Results Ledger

Updated: 2026-05-26.

This ledger collects the CUDA/static-annulus results mined from live repo docs,
archived campaign ledgers, and Gaal transcript archaeology. It is a reference
surface for campaign planning and paper consolidation. It is not a proof
authority and does not override `../AGENTS.md`,
`current-verification-spine.md`, or `experiment-contract.md`.

## Status Vocabulary

- **Accepted baseline:** current live docs or current verification spine treat
  the row family as the active executable gate.
- **Attached candidate:** historical detector evidence worth preserving, but
  needing rerun/postflight under the current compact spine before current
  acceptance.
- **Diagnostic:** useful detector/audit evidence with clear limits.
- **Scout:** directional or pressure evidence; useful for planning, not a
  claim.
- **Rejected/superseded:** invalid, killed, failed, stale, or replaced by a
  cleaner row.

All `SPANNING` and `MOAT` verdicts here are static-annulus detector verdicts
unless explicitly marked otherwise:

- `SPANNING` = `ANY-SPAN`;
- full-ingest `MOAT` = `ANY-SHELL-MOAT`.

They are not origin/source moat proofs.

## Current Summary

The battle-tested CUDA band engine has produced strong static-annulus evidence
through K40. The strongest current acceptance surface is K36 hardening around
`R_inner=80,000,000`. K38 and K40 have valuable diagnostic brackets and K40 is
the calibrated launchpad for K>40 scout design. There is no inspected K>40
result stream yet.

Layer 1 static-reach stitching telemetry from 2026-05-26 is recorded in
`lb-static-reach-streaming-telemetry-20260526.md`. The streaming/resident-width
implementation passed the full-static detector handoff equality gate and cut
production peak RSS roughly in half, but it is still tens of GiB because it
owns hundreds of millions of resident port atoms. Treat it as implementation
telemetry for `resumable-band`, not as new moat evidence.

## Ledger

| Status | K | Geometry | Result | Evidence notes |
|---|---:|---|---|---|
| Accepted baseline | 36 | `R_inner=80,000,000`; widths `17k`, `18k`, `19k`, `20k`; optional `32768` | Current MOAT-hardening matrix. | Live docs say exact BZ, zero overflow, `stats_v2`, and 512-tile postflight sample audit are the current gate. |
| Accepted baseline | 36 | `R_inner=80,000,000`; `R_outer=80,015,782` / `80,015,790` | Tsuchimura-adjacent `SPANNING` then `MOAT`. | Calibration note, not the primary gate. |
| Attached candidate | 36 | `W=32768`; `73,339,843..73,372,611` / `73,359,375..73,392,143` | Lower bracket: `SPANNING` then `MOAT`. | Zero overflow and legacy sample/cert evidence; needs current rerun for accepted hardening. |
| Diagnostic | 36 | `W=32768`; refined lower island | `72,739,560..72,740,648` observed `MOAT`; bracketed by `72,739,496` and `72,740,712` `SPANNING`. | Sample-audited diagnostic; no large-row span cert. |
| Diagnostic | 37-39 | `W=32768`; K36 lower-island radii | Probed rows preserve the K36 pattern. | Boundary/BZ stress telemetry only. |
| Attached candidate | 34 | Centers `30M`, `40M`, `50M`, `80M`; widths `16384`, `8192`, `4096`, `2048` | Local first `MOAT` rows; paired widest `SPANNING` rows one width lower. | Static-annulus evidence only, not Tsuchimura origin proof. |
| Diagnostic | 34 | Tsuchimura-scale `R ~= 24,289,452`; width ladder up to `131072` | Shell probes remain `SPANNING`. | Rejected as external truth gate because Tsuchimura K34 is origin-component, not shell-annulus. |
| Diagnostic | 26 | `R_inner=1,015,639`; widths `22682` / `22683` | Endpoint-adjacent transition: last `SPANNING`, first `MOAT`. | Diagnostic until non-square BZ acceptance is resolved. |
| Diagnostic | 26 | `R_inner=950,000`; widths `65643` / `65644` | Reconstruction transition: last `SPANNING`, first `MOAT`. | Near Tsuchimura endpoint, but different claim type. |
| Diagnostic aggregate | 26/32/34/36 | Telemetry calibration rows | 25 clean rows: K26 `3 MOAT/3 SPAN`, K32 `3/3`, K34 `2/3`, K36 `4/4`. | Useful for ranking near regions; weak as a moat classifier. Exact K32 rows need recovery before paper use. |
| Diagnostic | 38 | `W=32768`; `71,875,000` / `73,437,500` | Audited bracket: `SPANNING` then `MOAT`. | Endpoint diagnostic, zero overflow, SPAN path reconstructed; not origin proof. |
| Diagnostic | 40 | `W=32768`; high bracket | Refined to `978,000,000 SPANNING -> 979,500,000 MOAT`. | Older `980M MOAT` remains superseded endpoint evidence. |
| Diagnostic | 40 | `W=49152` | `937,500,000 SPANNING -> 940,625,000 MOAT`. | Larger-width bracket. |
| Diagnostic/audit | 40 | `W=262144` | Long correction: `860M SPANNING -> 870M MOAT`; later `855,000,001 MOAT` with `TILE_SAMPLE_AUDIT_PASS`. | `855,000,000` failed external BZ and is invalid; `845M SPANNING` side remains scout. |
| Scout | 40 | `W=524288`; `70M..800M`, `835M`, `840M` | Dense meshes through `835M` span cleanly; `840M` timeout candidate later killed by late `SPANNING`. | Candidate pressure only until killed; not moat evidence. |
| Scout | 40 | `W=720896` / `786432`; `840M` | `W=720896, R=840M` eventually `SPANNING`; `W=786432, R=840M` remains unresolved timeout pressure. | No `W=1048576` evidence was found in harvested docs. |
| Rejected/superseded | 38/40/34 | Assorted old rows and runbooks | Failed `k40-radius-refine`, killed `k40-980m-diag`, aborted K40 wide rows, invalid `W262144 R=855000000`, old K38 broad screens, K34 filename hazard. | Preserve only as provenance and negative evidence. |

## K40 Launchpad For K>40

K40 is the current calibration surface for K>40 design:

- `W=32768`: `978M SPANNING -> 979.5M MOAT`.
- `W=49152`: `937.5M SPANNING -> 940.625M MOAT`.
- `W=262144`: corrected/audited evidence around `855,000,001` and `870M`.
- `W=524288+`: pressure probes are useful, but timeout is not a verdict.

K>40 work should start as a bounded scout contract: exact K, radius family,
width family, BZ policy, timeout semantics, artifact contract, and promotion
rules. Do not write theorem or proof language for K>40 until rows exist and
the relevant gates pass.

## Known Gaps

- Exact artifact paths for the current K36 `17k` / `18k` / `19k` / `20k`
  matrix need a final pointer pass before paper citation.
- K32 telemetry rows are counted but not listed by exact radius/width here.
- Some large-row `SPANNING` endpoints lack accepted coordinate certificates.
- K34 current Tsuchimura-bound campaign planning has no final accepted result
  bundle in the harvested reference docs.
- No K>40 campaign ledger rows have been inspected.

## Source Pointers

Live docs:

- `../README.md`
- `../AGENTS.md`
- `current-verification-spine.md`
- `attached-static-annulus-moats.md`

Archive ledgers:

- `archive/campaign-ledgers/2026-05-04-static-annulus-evidence-index.md`
- `archive/campaign-ledgers/k26-static-annulus-diagnostics.md`
- `archive/campaign-ledgers/k26-k36-telemetry-calibration-20260508.md`
- `archive/campaign-ledgers/k34-k36-centered-annulus-sweep-2026-05-04.md`
- `archive/campaign-ledgers/k34-static-annulus-gauntlet-2026-05-04.md`
- `archive/campaign-ledgers/k36-lowest-moat-refinement-20260508.md`
- `archive/campaign-ledgers/k37-k39-k36-lowest-moat-scout-20260508.md`
- `archive/campaign-ledgers/k38-k40-campaign-status-2026-05-04.md`
- `archive/campaign-ledgers/k38-k40-correctness-audit-2026-05-04.md`
- `archive/campaign-ledgers/k40-current-gate-campaign-20260509.md`
- `archive/campaign-ledgers/k40-below-850-fine-probe-plan-20260516.md`
- `archive/campaign-ledgers/k40-w524288-fine-and-855000001-audit-20260516.md`
- `archive/campaign-ledgers/k40-w786432-840m-candidate-and-w720896-clean-run-20260518.md`

Gaal sessions used during the 2026-05-26 audit:

- `36a6276e`: K38/K40 static-annulus campaign.
- `748aba2b`: parallel audit and oracle cleanup.
- `5cc73f9d`: K40 width campaign continuation.
- `40e4829f`: K40 wide probes.
- `a7368d6f`: K36 Tsuchimura/performance wave.
- `fbb3446c`: compact postflight and K36 hardening matrix.
- `39ae9a4d`: lower K36 bracket.
