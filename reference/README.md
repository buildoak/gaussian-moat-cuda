# Gaussian Moat Reference Map

This directory is for compact operational references. Historical plans, campaign
ledgers, and long evidence reports live under `reference/archive/` so they can
be cited without competing with the current verification doctrine.

Always read first:

1. `../AGENTS.md` - repo authority, claim semantics, compact gate spine.
2. `experiment-contract.md` - how to run and report campaign experiments.
3. `../verification/README.md` and `../verification/postflight/README.md` -
   independent verifier tools and post-flight runner usage.

Static-annulus first-read:

1. `current-verification-spine.md` - static-annulus gate spine and proof-status
   vocabulary.
2. `cuda-band-engine-playbook.md` - what the CUDA band engine does, how to use
   it, and what it does not prove.
3. `cuda-band-results-ledger.md` - compact ledger of harvested K26/K34/K36/K38/K40
   static-annulus results and status labels.
4. `attached-static-annulus-moats.md` - attached lower-K static-annulus rows and
   proof-status cautions.

Lower-bound source-propagation first-read:

1. `../methodology/source-propagation-band-stitching.md` - separator state,
   no-rewire, bridge obligations, and first-plus-rolling-last state.
2. `../tiles-maxxing/lb-source-propagation/README.md` - current LB
   implementation surfaces, gates, and non-claim boundaries.
3. `../tiles-maxxing/lb-source-propagation/docs/lb-handoff-redesign.md` - live
   handoff and targeted last-band refinement design.
4. `lb-static-reach-streaming-telemetry-20260526.md` - latest Layer 1
   static-reach streaming/resident-width results and memory model.
5. `../tiles-maxxing/lb-source-propagation/docs/tile-frontier-streaming-redesign.md`
   only when touching streaming.
6. `../tiles-maxxing/lb-source-propagation/docs/k26-tsuchimura-readiness.md`
   only when touching K26.
7. Remote/overnight runbooks only when paid remote execution is explicitly
   authorized.

## Live References

| File | Role |
|---|---|
| `current-verification-spine.md` | Active static-annulus verification gates, demoted tools, and sample policy. |
| `cuda-band-engine-playbook.md` | Operator playbook for battle-tested CUDA band rows, resumable campaign use, and non-claim boundaries. |
| `cuda-band-results-ledger.md` | Compact results ledger for harvested CUDA/static-annulus rows, K40 launchpad status, and known gaps. |
| `lb-static-reach-streaming-telemetry-20260526.md` | Latest Layer 1 static-reach streaming/resident-width telemetry, RSS comparison, and 2-3 GiB memory-model warning. |
| `attached-static-annulus-moats.md` | Attached lower-K36 and local K34 static-annulus moat evidence, with proof-status cautions. |
| `experiment-contract.md` | Operational contract for running/reporting campaign experiments. |
| `optimization-safety-checklist.md` | Do-not-break checklist for math/TileOp/port/verdict changes. |
| `performance-report-template.md` | Performance report shape. |

## Archive

| Directory | Contents | Authority |
|---|---|---|
| `archive/campaign-ledgers/` | May 2026 K26/K34/K36/K37-K40 campaign notes, scout plans, diagnostic reports, and evidence indexes. | Provenance only. |
| `archive/implemented-plans/` | Verification/postflight/LB seed plans and evidence reports that have been implemented or superseded. | Historical context only. |
| `archive/performance-and-handoffs/` | Old optimization plans, agentic workflow notes, wave reports, and worker handoffs. | Provenance only. |
| `archive/pre-push-secret-check.md` | Historical pre-push credential scan runbook. | Use only when pushing/publishing is requested. |
| `archive/heavy-history-cleanup-plan.md` | Historical large-history cleanup plan. | Use only when history cleanup is explicitly authorized. |

Archived files may contain obsolete sample budgets, gate ladders, branch names,
and proof language. Use them as evidence pointers, not as current instructions.
