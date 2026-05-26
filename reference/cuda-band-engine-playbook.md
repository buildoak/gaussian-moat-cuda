# CUDA Band Engine Playbook

Updated: 2026-05-26.

This is the operator playbook for the battle-tested CUDA band engine. It is a
reference surface, not a proof specification. Math authority remains in
`../methodology/tile-operator-definition-v-claude.md`; repo routing and claim
semantics remain in `../AGENTS.md`; active acceptance gates remain in
`current-verification-spine.md`.

## Scope

The CUDA band engine is the current production surface for static-annulus
campaign rows:

```text
full-octant band geometry
-> CUDA TileOp production
-> streaming compositor ingestion
-> ANY-SPAN / ANY-SHELL-MOAT detector verdict
-> postflight profile, sample, and certificate checks
```

It is designed to be reused for long-running CUDA band campaigns, including
resumable high-radius and high-K scouts. Its first job in that mode is
lightweight continuation: checkpoint, restart, band-to-band stitching, harvest,
and replay with low overhead.

## What It Does

- Enumerates full-octant static-annulus tile grids for explicit `K`,
  `R_inner`, `R_outer`, width, and region.
- Runs CUDA K1-K5 TileOp production on a CUDA host.
- Streams emitted TileOps into the host compositor.
- Produces static-annulus verdicts:
  - `SPANNING` means `ANY-SPAN`;
  - full-ingest `MOAT` means `ANY-SHELL-MOAT`.
- Emits profile/audit telemetry, deterministic tile samples, and optional
  SPANNING coordinate certificates.
- Supports current postflight gates: Exact Profile, Independent Tile Sample,
  SPANNING Cert, and MOAT Hardening.

## What It Does Not Do

- It does not prove an origin-connected or source-connected moat.
- It does not implement global threshold proofs.
- It does not make `MOAT_PROOF_PASS` reachable.
- It does not turn archive ledgers into authority.
- It does not make timeout pressure into a `MOAT`.
- It does not make K>40 rows claim-grade by default.

Source/origin propagation is a separate mode. Use it when the task explicitly
asks for source survival, source death, last-live/first-dead refinement, or
source certificates. Do not make source propagation the hot path for ordinary
static-annulus, high-K, or prolonged detector campaigns.

## Campaign Modes

| Mode | Use when | Hot path |
|---|---|---|
| `paper` | Consolidating already-mined evidence and methods. | Docs, figures, claim language, provenance checks. |
| `static-annulus` | Running or verifying detector rows. | CUDA engine plus compact postflight spine. |
| `resumable-band` | Running prolonged campaigns that must stop/continue. | Band checkpoints, low-overhead stitching, harvest/replay. |
| `source-origin` | Asking whether a source/origin component survives or dies. | Exact frontier handoff plus source bits. |
| `proof-refinement` | Promoting detector evidence toward stronger claims. | Explicit proof obligation plus independent checker. |

Declare the mode before changing code, docs, or campaign plans.

## Run Contract

Every serious CUDA band run report should include:

- branch and commit;
- machine, GPU, driver, CUDA version;
- build flags and `K_SQ`;
- exact command line;
- `K`, `R_inner`, `R_outer`, width, region, and chunk size;
- early-exit setting and whether early exit fired;
- produced and ingested tile counts;
- BZ status and overflow counters;
- profile path and sample/cert artifact paths;
- postflight status and proof status;
- total wall time, CUDA K1-K5 time, compositor time, and throughput.

For `MOAT`, full ingest is mandatory. For accepted `SPANNING`, a coordinate
certificate is mandatory. Production sample audits normally use `512`
manifested deterministic samples.

## Acceptance Gates

Use `current-verification-spine.md` as the active gate owner.

| Gate | Required for | Meaning |
|---|---|---|
| Exact Profile | Accepted/profile rows. | Shape, BZ, overflow, metadata, and `stats_v2` are coherent. |
| Independent Tile Sample | Accepted/profile rows with samples. | Emitted samples match independent TileOp regeneration. |
| SPANNING Cert | Accepted `SPANNING`. | Independent coordinate path connects `geo_I` to `geo_O`. |
| MOAT Hardening | Current `MOAT`. | Full-ingest detector run plus clean profile/sample evidence. |

`MOAT Hardening` is not an independent negative certificate. It should be
reported as detector/audit evidence unless a future proof system changes the
spine.

## Resumable Band Campaign Contract

The lightweight prolonged-run protocol should stay simple:

```text
band i local CUDA evidence
-> compact checkpoint / handoff
-> band i+1 continuation
-> replayable harvest ledger
-> targeted refinement near the interesting transition
```

The first durable artifact should be a small schema that can answer:

- which band geometry was run;
- which build and command produced it;
- what checkpoint state is needed to continue;
- what artifacts prove the run can be replayed;
- whether the row is scout, diagnostic, accepted detector evidence, or proof
  refinement input.

Do not carry whole-band histories, large inventories, or proof burdens through
the hot path unless the declared mode requires them.

## Above-K40 Use

K>40 work is currently scout-design territory. Use K40 as the calibrated
launchpad:

1. choose the exact K, radius, width family, BZ policy, and timeout budget;
2. run static-annulus scout rows with explicit semantics;
3. promote only rows with clean profile/sample/cert evidence;
4. add source/origin gates only if the claim actually names source/origin
   survival or death.

Timeout pressure means candidate pressure. It is not a `MOAT`.

## Failure Taxonomy

- **Rejected:** BZ failure, overflow, malformed profile, failed sample audit,
  missing required SPANNING cert, or incoherent run contract.
- **Scout:** useful directional evidence with incomplete acceptance gates.
- **Diagnostic:** stronger than scout but still not accepted claim evidence.
- **Attached candidate:** useful historical detector evidence that needs
  rerun/postflight under the current spine.
- **Accepted detector evidence:** compact spine passed for the claim actually
  made.
- **Proof evidence:** reserved for future stronger proof systems with explicit
  independent checkers.

## Reporting Rule

Say exactly what the row proves:

```text
This is a static-annulus ANY-SHELL-MOAT detector/audit row.
It is not an origin-component moat proof.
```

If the sentence feels too weak, the fix is not rhetoric. The fix is a stronger
mode, a stronger artifact, and a stronger independent checker.
