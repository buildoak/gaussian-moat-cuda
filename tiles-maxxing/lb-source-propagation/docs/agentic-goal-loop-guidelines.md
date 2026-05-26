# Agentic Goal Loop Guidelines

Updated: 2026-05-26.

This repo is an agentic-first workbench, but not an unbounded rewrite surface.
Autonomous loops are allowed to make code progress only inside explicit goals,
with a declared mode, tight impact area, and proof-status discipline.

## Loop Contract

Before a goal-driven loop starts, write or receive:

```text
Goal:
Mode:
Impact area:
Non-goals:
Local gate:
Remote gate:
Artifact path:
Stop rule:
Report shape:
```

If any field is missing, the loop must fill it in conservatively before
changing code. If the goal needs paid Vast execution, current-session user
authorization is required before renting or using a paid instance.

Before changing code, also read `code-quality-guidelines.md` and name the
owned files in the loop contract.

## Default Goal

For LB code alignment, the default goal is:

```text
Make prolonged CUDA-band runs restartable, replayable, and harvestable with
minimal hot state. Keep source/origin propagation as an opt-in overlay for
claims that explicitly require certified source survival or death.
```

## Operating Rules

- Declare one mode: `resumable-band`, `source-origin`, `proof-refinement`, or
  `static-annulus`.
- Touch only the impact area needed for that mode.
- Keep new code modular; do not grow oversized files without naming why
  extraction is not the smaller safe move.
- Prefer small artifact contracts and verifier checks over broad architecture.
- Keep generated outputs under ignored `artifacts/`, `runs/`, `tmp/`, or
  `_archive/workbench/`.
- Do not commit pulled Vast run outputs unless the user explicitly asks.
- Never promote diagnostic output to claim evidence.
- Never make source propagation a hidden dependency of static-annulus or high-K
  detector work.
- Never destroy a Vast instance unless it is proven to be owned by this
  Gaussian Moat CUDA workstream and cleanup was authorized.

## Local Gate Ladder

Use the smallest gate that covers the edit:

| Edit type | Minimum local gate |
|---|---|
| Markdown/routing only | `git diff --check`. |
| `source_propagation` logic | LB CTest target plus relevant verifier fixtures. |
| `stream_checkpoint` logic | `test_stream_checkpoint` plus runner resume test. |
| `tileop_port_stream` logic | stream CTest plus stream equivalence shell test. |
| artifact checker/schema | checker hostile fixtures plus schema contract test. |
| remote script | shell syntax/read-only dry run plus artifact checker if applicable. |

The full local gate is:

```bash
tiles-maxxing/lb-source-propagation/scripts/check_phase1_local_gates.sh
```

Use it before handing off a substantial code change or before launching remote
compute from a changed branch.

## Vast Loop Gate

Remote/Vast loops must:

1. state why local tests are insufficient;
2. record the intended instance owner as `gaussian-moat-cuda`;
3. write an instance ledger row before any cleanup action;
4. run inside `tmux` for commands over 5 seconds;
5. write remote environment, branch, commit, command, and proof status;
6. pull artifacts before cleanup;
7. run the relevant local artifact checker after pull;
8. report exact instance id, artifact path, and cleanup status.

Remote output is `DIAGNOSTIC_NON_CLAIM` unless an independent checker promotes
it.

## Stop Rules

Stop the loop and report instead of continuing when:

- the work would broaden beyond the declared impact area;
- the next fix needs a stronger campaign mode than declared;
- local gates fail twice with different root causes;
- a remote artifact lacks branch/commit/proof-status metadata;
- a Vast instance cannot be attributed to Gaussian Moat CUDA ownership;
- a claim label would exceed the available evidence;
- the implementation starts carrying historical inventory or full slabs through
  the hot path without an explicit proof-tier goal.

## Report Shape

Return:

```text
Done:
Mode:
Changed:
Evidence:
Artifacts:
Vast:
Blocked:
Next:
```

Evidence must name commands or files. Vast must state `not used` or list
instance id, owner, artifact path, and cleanup status.
