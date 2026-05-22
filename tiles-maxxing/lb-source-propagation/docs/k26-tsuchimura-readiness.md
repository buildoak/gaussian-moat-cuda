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
   BZ-clean. The exact schedule diagnostic shows the nominal 124-row
   width-8192 schedule is not BZ-clean at rows `15`, `58`, and `75`, then
   repairs the internal boundaries `122880`, `475136`, and `622592` down by
   `1`. The repaired schedule is BZ-clean but remains diagnostic until it is
   bound into full-run metadata.
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
- accepted K26 non-square BZ evidence bound to the repaired schedule;
- an accepted full-scale `SOURCE_DEAD_CERT` artifact. The current independent
  draft checker validates a listed positive source path, negative guard, and
  inventory count/digest consistency, but the real K26 chain and BZ acceptance
  are still missing.

The exact command contract is:

```bash
k26_source_run_commands
```

It emits the row-0 coordinate prefix command and the repaired TileOp-port
continuation schedule. The row-0 prefix command targets the canonical-octant
representative `376039 + 943460i`; the comparison back to Tsuchimura's
`943460 + 376039i` is by Gaussian-unit and conjugation symmetry. This command
is not part of the local smoke gate. A Mac Mini probe of the row-0 prefix was
still CPU-bound after eight minutes with no completed artifact, so row-0 prefix
execution should be treated as paid-run work under the Vast budget cap unless a
smaller diagnostic radius is being tested.

## Stop Conditions

Stop and report without claiming reproduction if:

- source state depends on transient TileOp group labels or union-find roots;
- any overflow appears in a source/origin row;
- coordinate-to-port seam bridging leaves unbridged coordinate carry atoms or
  first-band port death without an accepted proof that this is irrelevant;
- BZ evidence is missing or mismatched for `K=26`;
- terminal inventory cannot preserve retired source components;
- remote runtime or memory threatens the agreed budget.
