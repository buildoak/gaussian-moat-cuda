#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  vast_sidecar_smoke_guard.sh [--execute] [--max-dph PRICE] [--max-budget USD]
                              [--offer-id ID] [--remote DIR] [--pull-dir DIR]

Dry-run by default. Searches for a single RTX 4090 offer, enforces the price
cap, and prints the exact create/deploy/smoke/pull commands. With --execute it
creates the instance only; deploy/smoke/pull remain explicit operator steps
because SSH readiness and cleanup need human-visible instance metadata.

Hard defaults from the LB source-propagation goal:
  --max-dph     0.37
  --max-budget  1.50

This script never destroys an instance. Cleanup remains an explicit operator
decision after artifacts are pulled.
USAGE
}

execute=0
max_dph="0.37"
max_budget="1.50"
offer_id=""
remote_dir="/workspace/gaussian-moat-cuda"
pull_dir="tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-pull"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --execute)
      execute=1
      shift
      ;;
    --max-dph)
      max_dph="$2"
      shift 2
      ;;
    --max-budget)
      max_budget="$2"
      shift 2
      ;;
    --offer-id)
      offer_id="$2"
      shift 2
      ;;
    --remote)
      remote_dir="$2"
      shift 2
      ;;
    --pull-dir)
      pull_dir="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! command -v vastai >/dev/null 2>&1; then
  echo "vastai CLI not found" >&2
  exit 2
fi

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"
local_head="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
local_branch="$(git branch --show-current 2>/dev/null || echo unknown)"

shell_join() {
  local out=""
  local arg
  for arg in "$@"; do
    printf -v arg '%q' "$arg"
    out+="${out:+ }${arg}"
  done
  printf '%s\n' "$out"
}

filter="gpu_name=RTX_4090 cuda_vers>=12.0 disk_space>=40 num_gpus=1 dph<=${max_dph} reliability>=0.95"

echo "search_filter=$filter"
offers="$(vastai search offers "$filter" -o 'dph' --raw)"
if [[ -z "$offers" || "$offers" == "[]" ]]; then
  echo "NO_QUALIFYING_OFFER"
  echo "No RTX 4090 offer satisfied max_dph=${max_dph}; no rental attempted."
  exit 3
fi

if [[ -z "$offer_id" ]]; then
  offer_id="$(python3 -c 'import json,sys; data=json.load(sys.stdin); print(data[0]["id"])' <<<"$offers")"
fi

offer_json="$(python3 -c 'import json,sys; oid=int(sys.argv[1]); data=json.load(sys.stdin); matches=[x for x in data if int(x["id"])==oid]; print(json.dumps(matches[0] if matches else {}))' "$offer_id" <<<"$offers")"
if [[ "$offer_json" == "{}" ]]; then
  echo "offer_id ${offer_id} is not present in the capped search results" >&2
  exit 4
fi

offer_dph="$(python3 -c 'import json,sys; print(json.load(sys.stdin)["dph_total"])' <<<"$offer_json")"
python3 - "$offer_dph" "$max_dph" <<'PY'
import sys
dph=float(sys.argv[1])
cap=float(sys.argv[2])
if not dph <= cap:
    raise SystemExit(f"offer price {dph} exceeds cap {cap}")
PY

soft_hours="$(python3 - "$max_budget" "$offer_dph" <<'PY'
import sys
budget=float(sys.argv[1])
dph=float(sys.argv[2])
print(f"{budget / dph:.2f}")
PY
)"

echo "QUALIFYING_OFFER id=${offer_id} dph=${offer_dph} budget_hours=${soft_hours}"
echo "LOCAL_SOURCE branch=${local_branch} head=${local_head}"

create_cmd=(vastai create instance "$offer_id"
  --image pytorch/pytorch:2.5.1-cuda12.4-cudnn9-devel
  --disk 40 --ssh
  --onstart-cmd 'apt-get update && apt-get install -y tmux cmake ninja-build rsync')

cat <<EOF
CREATE:
  $(shell_join "${create_cmd[@]}")

After instance is ready, set:
  ID=<instance_id>
  HOST=<ssh_host>
  PORT=<ssh_port>
  SSH_CMD="ssh -o StrictHostKeyChecking=accept-new -p \$PORT root@\$HOST"
  LOCAL_HEAD=${local_head}
  LOCAL_BRANCH=${local_branch}

DEPLOY:
  rsync -avz --delete --exclude '.git' --exclude 'build*/' --exclude '**/build*/' --exclude '**/artifacts/' --exclude '**/results/' --exclude '**/profiles/' --exclude '**/runs/' --exclude '**/tmp/' --exclude '**/*.bin' --exclude '**/*.log' -e "ssh -o StrictHostKeyChecking=accept-new -p \$PORT" ./ root@\$HOST:${remote_dir}/
  printf "deployed_local_head=%s\ndeployed_local_branch=%s\n" "\$LOCAL_HEAD" "\$LOCAL_BRANCH" | \$SSH_CMD "mkdir -p /workspace/lb-source-remote-smoke && cat > /workspace/lb-source-remote-smoke/deployed_source.txt"

REMOTE_SMOKE:
  \$SSH_CMD "cd ${remote_dir} && tiles-maxxing/lb-source-propagation/scripts/remote_sidecar_smoke.sh --repo ${remote_dir} --out-dir /workspace/lb-source-remote-smoke"

PULL:
  mkdir -p ${pull_dir}
  rsync -avz -e "ssh -o StrictHostKeyChecking=accept-new -p \$PORT" root@\$HOST:/workspace/lb-source-remote-smoke/ ${pull_dir}/

ACCEPTANCE_CHECK:
  grep -q "100% tests passed" ${pull_dir}/ctest.log
  test "\$(grep -c '^ *[0-9][0-9]*/14 Test' ${pull_dir}/ctest.log)" -eq 14
  grep -q "100% tests passed" ${pull_dir}/verification-ctest.log
  test "\$(grep -c '^ *[0-9][0-9]*/43 Test' ${pull_dir}/verification-ctest.log)" -eq 43
  grep -q "deployed_local_head=${local_head}" ${pull_dir}/deployed_source.txt
EOF

if [[ "$execute" -eq 0 ]]; then
  echo "DRY_RUN_ONLY"
  exit 0
fi

"${create_cmd[@]}"

cat <<'EOF'
Instance creation requested. Wait for SSH readiness, then run the DEPLOY,
REMOTE_SMOKE, PULL, and ACCEPTANCE_CHECK commands printed above. This script
intentionally does not destroy instances.
EOF
