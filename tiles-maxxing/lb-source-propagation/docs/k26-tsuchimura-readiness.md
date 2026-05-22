# sqrt(26) Tsuchimura Readiness

This note is the execution guard for the first source/origin comparison target.
It is not a result claim.

## Target

- Claim label: `SOURCE_ORIGIN_K26`, not static-annulus `ANY-SPAN` or
  `ANY-SHELL-MOAT`.
- Expected endpoint: `943460 + 376039i`.
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
   BZ-clean. The current exact schedule diagnostic shows the nominal 124-row
   width-8192 schedule is not BZ-clean: rows `15`, `58`, and `75` need row
   shifts or adaptive boundary repair before any claim run.
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

- positive source path or certificate chain to `943460 + 376039i`;
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
  accepted lemma and verifier gate.
- accepted terminal inventory handling for count/digest/max norm/tie set at
  14.5B-member scale;
- accepted K26 non-square BZ evidence after repairing nominal dirty rows;
- an accepted full-scale `SOURCE_DEAD_CERT` artifact. The current independent
  draft checker validates a listed positive source path, negative guard, and
  inventory count/digest consistency, but the real K26 chain and BZ acceptance
  are still missing.

## Stop Conditions

Stop and report without claiming reproduction if:

- source state depends on transient TileOp group labels or union-find roots;
- any overflow appears in a source/origin row;
- coordinate-to-port seam bridging leaves unbridged coordinate carry atoms or
  first-band port death without an accepted proof that this is irrelevant;
- BZ evidence is missing or mismatched for `K=26`;
- terminal inventory cannot preserve retired source components;
- remote runtime or memory threatens the agreed budget.
