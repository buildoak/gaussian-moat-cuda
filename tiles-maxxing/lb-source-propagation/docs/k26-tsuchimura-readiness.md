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

For a terminal guard, use `R_final >= 1015644`; tighter values such as
`1015639` leave the expected endpoint inside the final guard and cannot support
a death certificate.

## Required Evidence Before Run

1. Local sidecar CMake/CTest passes from a fresh build directory.
2. Full independent `verification/` CTest passes from a fresh build directory.
3. A remote 4090 sidecar smoke passes under the budget cap:
   - price `< 0.37 USD/hour`;
   - total budget `<= 1.50 USD`;
   - no long K32 launch.
4. The run is wired to source/origin seed logic, not `geo_I` flags.
5. Every source/origin proof row rejects overflow.
6. Non-square `K=26` has external per-row BZ evidence, or the row is labeled
   diagnostic rather than accepted.

## Certificate Gap To Close

`SOURCE_TERMINAL` is not enough. A `SOURCE_DEAD_CERT` needs:

- positive source path or certificate chain to `943460 + 376039i`;
- negative final guard proof at `R_final >= 1015644`;
- terminal inventory with count/digest/max norm/tie set;
- stable artifact hashes for carry manifests and source profile drafts;
- commit/build identity and BZ evidence in the profile metadata.

## Stop Conditions

Stop and report without claiming reproduction if:

- source state depends on transient TileOp group labels or union-find roots;
- any overflow appears in a source/origin row;
- BZ evidence is missing or mismatched for `K=26`;
- terminal inventory cannot preserve retired source components;
- remote runtime or memory threatens the agreed budget.
