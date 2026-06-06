# Security Posture (read before claiming privacy)

This testbed is a **research / stress-test platform**, not a production
secure-aggregation system. Be precise about what is and isn't proven.

## What holds

- **All-party MPC path** (`bench_mpc`, `meter_node`): additive secret sharing
  over Fp (p = 2^31 - 1). A single share reveals nothing; correctness validated
  for N=2 and N=6.
- **Central secure aggregation** (`secure_agg_server` + `secure_agg_meter`):
  each meter sends one masked value; deterministic pairwise masks cancel in the
  sum. The aggregator sees only masked submissions and the final total.

## What does NOT hold (do not claim)

- **Mask key is shared with the harness.** Masks are derived from one
  `--mask-key` passed to every meter (`pair_mask` / splitmix64). This is fine
  for stress testing the aggregation path; it is **not** a key-agreement
  protocol. A party that knows the key can unmask. Production needs pairwise
  key establishment (e.g. ECDH or pre-shared pair keys) so the aggregator never
  learns any pairwise mask.
- **No dropout resilience.** If a meter fails to submit, its pairwise masks do
  not cancel and the aggregate is wrong. The server now *reports* missing ids
  (`MISSING_IDS`, `--missing-out`) but does **not** recover them.
- **No authentication.** The packet carries `magic` / `version` / `type` /
  `test_id`. `test_id` only prevents *accidental* cross-run contamination — it
  is not a MAC and does not stop a malicious client.
- **No collusion analysis.** Security against colluding parties is not modeled.
- **Linux process / cgroup limits are approximations**, not microcontroller
  constraints. This is not an STM32/MSP430 deployment.

## Correct one-line claim

> The testbed validates small-N all-party MPC and demonstrates high-N central
> secure aggregation feasibility on two Raspberry Pis using constrained Linux
> processes.
