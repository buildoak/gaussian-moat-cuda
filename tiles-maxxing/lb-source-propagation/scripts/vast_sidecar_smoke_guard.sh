#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  vast_sidecar_smoke_guard.sh [--execute] [--max-dph PRICE] [--max-budget USD]
                              [--k-sq N]
                              [--offer-id ID] [--remote DIR] [--pull-dir DIR]
                              [--offer-wait-seconds N]
                              [--offer-poll-seconds N]
                              [--wait-ssh-seconds N] [--ssh-poll-seconds N]
                              [--stop-on-ssh-timeout]
                              [--run-remote-smoke] [--destroy-on-exit]

Dry-run by default. Searches for a single RTX 4090 offer, enforces the price
cap, and prints the exact create/deploy/smoke/pull commands. With --execute it
creates the instance only by default. Add --run-remote-smoke to make the script
wait for SSH, deploy the current tree, run remote_sidecar_smoke.sh, pull
artifacts, and run the local artifact acceptance checker. Add --destroy-on-exit
to destroy the created instance after success or failure.

Optional offer polling handles transient market races, and optional SSH
readiness polling can stop or destroy a newly-created instance when SSH never
opens.

Hard defaults from the LB source-propagation goal:
  --max-dph     0.37
  --max-budget  1.50
  --k-sq        26

This script destroys an instance only when --destroy-on-exit is supplied.
USAGE
}

execute=0
max_dph="0.37"
max_budget="1.50"
offer_id=""
k_sq="26"
remote_dir="/workspace/gaussian-moat-cuda"
pull_dir="tiles-maxxing/lb-source-propagation/artifacts/vast-smoke-pull"
offer_wait_seconds="0"
offer_poll_seconds="30"
wait_ssh_seconds="0"
ssh_poll_seconds="10"
stop_on_ssh_timeout=0
run_remote_smoke=0
destroy_on_exit=0
created_instance_id=""

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
    --k-sq)
      k_sq="$2"
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
    --offer-wait-seconds)
      offer_wait_seconds="$2"
      shift 2
      ;;
    --offer-poll-seconds)
      offer_poll_seconds="$2"
      shift 2
      ;;
    --wait-ssh-seconds)
      wait_ssh_seconds="$2"
      shift 2
      ;;
    --ssh-poll-seconds)
      ssh_poll_seconds="$2"
      shift 2
      ;;
    --stop-on-ssh-timeout)
      stop_on_ssh_timeout=1
      shift
      ;;
    --run-remote-smoke)
      run_remote_smoke=1
      shift
      ;;
    --destroy-on-exit)
      destroy_on_exit=1
      shift
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

require_nonnegative_integer() {
  local value="$1"
  local label="$2"
  if ! [[ "$value" =~ ^[0-9]+$ ]]; then
    echo "${label} must be a nonnegative integer: $value" >&2
    exit 2
  fi
}

require_nonnegative_integer "$wait_ssh_seconds" "--wait-ssh-seconds"
require_nonnegative_integer "$ssh_poll_seconds" "--ssh-poll-seconds"
require_nonnegative_integer "$offer_wait_seconds" "--offer-wait-seconds"
require_nonnegative_integer "$offer_poll_seconds" "--offer-poll-seconds"
if [[ "$wait_ssh_seconds" != "0" && "$ssh_poll_seconds" == "0" ]]; then
  echo "--ssh-poll-seconds must be positive when SSH readiness polling is enabled" >&2
  exit 2
fi
if [[ "$offer_wait_seconds" != "0" && "$offer_poll_seconds" == "0" ]]; then
  echo "--offer-poll-seconds must be positive when offer polling is enabled" >&2
  exit 2
fi
if [[ "$run_remote_smoke" -eq 1 && "$wait_ssh_seconds" == "0" ]]; then
  wait_ssh_seconds="600"
fi

cleanup_instance() {
  local status="$?"
  if [[ "$destroy_on_exit" -eq 1 && -n "$created_instance_id" ]]; then
    echo "DESTROYING_INSTANCE_ON_EXIT id=${created_instance_id}" >&2
    vastai destroy instance "$created_instance_id" -y >&2 || true
  fi
  return "$status"
}
trap cleanup_instance EXIT

parse_created_instance_id() {
  CREATE_OUTPUT="$1" python3 <<'PY'
import os
import re

text = os.environ.get("CREATE_OUTPUT", "")
match = re.search(r"['\"]new_contract['\"]\s*:\s*([0-9]+)", text)
if match:
    print(match.group(1))
PY
}

instance_ssh_fields() {
  local instance_id="$1"
  vastai show instances --raw | python3 -c '
import json
import sys

instance_id = int(sys.argv[1])
for item in json.load(sys.stdin):
    if int(item.get("id", -1)) == instance_id:
        print(
            "{} {} {} {}".format(
                item.get("cur_state") or "",
                item.get("intended_status") or "",
                item.get("ssh_host") or "",
                item.get("ssh_port") or "",
            )
        )
        break
' "$instance_id"
}

wait_for_ssh_ready() {
  local instance_id="$1"
  local deadline="$((SECONDS + wait_ssh_seconds))"
  local state intended host port
  while (( SECONDS <= deadline )); do
    state=""
    intended=""
    host=""
    port=""
    read -r state intended host port < <(instance_ssh_fields "$instance_id" || true) || true
    if [[ -n "${host:-}" && -n "${port:-}" ]]; then
      echo "SSH_PROBE id=${instance_id} state=${state:-unknown} intended=${intended:-unknown} host=${host} port=${port}"
      if ssh -o StrictHostKeyChecking=accept-new \
          -o ConnectTimeout=8 \
          -p "$port" "root@${host}" 'echo SSH_READY && hostname' >/dev/null; then
        echo "SSH_READY id=${instance_id} host=${host} port=${port}"
        return 0
      fi
    else
      echo "SSH_PROBE id=${instance_id} metadata_unavailable"
    fi
    sleep "$ssh_poll_seconds"
  done

  echo "SSH_TIMEOUT id=${instance_id} waited_seconds=${wait_ssh_seconds}" >&2
  if [[ "$stop_on_ssh_timeout" -eq 1 ]]; then
    echo "STOPPING_UNREADY_INSTANCE id=${instance_id}" >&2
    vastai stop instance "$instance_id"
  fi
  return 5
}

require_ssh_endpoint() {
  local instance_id="$1"
  local state intended host port
  read -r state intended host port < <(instance_ssh_fields "$instance_id" || true) || true
  if [[ -z "${host:-}" || -z "${port:-}" ]]; then
    echo "SSH endpoint unavailable for instance ${instance_id}" >&2
    exit 5
  fi
  printf '%s %s\n' "$host" "$port"
}

run_remote_smoke_gate() {
  local instance_id="$1"
  local host port ssh_cmd rsync_ssh
  read -r host port < <(require_ssh_endpoint "$instance_id")
  ssh_cmd=(ssh -o StrictHostKeyChecking=accept-new -p "$port" "root@${host}")
  rsync_ssh="ssh -o StrictHostKeyChecking=accept-new -p ${port}"

  rm -rf "$pull_dir"
  mkdir -p "$pull_dir"

  echo "DEPLOYING_SOURCE id=${instance_id} host=${host} port=${port}"
  rsync -avz --delete \
    --exclude '.git' \
    --exclude 'build*/' \
    --exclude '**/build*/' \
    --exclude '**/artifacts/' \
    --exclude '**/results/' \
    --exclude '**/profiles/' \
    --exclude '**/runs/' \
    --exclude '**/tmp/' \
    --exclude '**/*.bin' \
    --exclude '**/*.log' \
    -e "$rsync_ssh" \
    ./ "root@${host}:${remote_dir}/"

  printf "deployed_local_head=%s\ndeployed_local_branch=%s\n" \
      "$local_head" "$local_branch" |
    "${ssh_cmd[@]}" \
      "mkdir -p /workspace/lb-source-remote-smoke && cat > /workspace/lb-source-remote-smoke/deployed_source.txt"

  echo "RUNNING_REMOTE_SMOKE id=${instance_id}"
  "${ssh_cmd[@]}" \
    "cd ${remote_dir} && tiles-maxxing/lb-source-propagation/scripts/remote_sidecar_smoke.sh --repo ${remote_dir} --k-sq ${k_sq} --out-dir /workspace/lb-source-remote-smoke"

  echo "PULLING_REMOTE_SMOKE_ARTIFACTS id=${instance_id}"
  rsync -avz -e "$rsync_ssh" \
    "root@${host}:/workspace/lb-source-remote-smoke/" \
    "${pull_dir}/"

  echo "CHECKING_REMOTE_SMOKE_ARTIFACTS id=${instance_id}"
  tiles-maxxing/lb-source-propagation/scripts/check_remote_smoke_artifacts.sh \
    "$pull_dir" \
    --expect-head "$local_head" \
    --expect-branch "$local_branch" \
    --expect-k-sq "$k_sq"
  echo "REMOTE_VAST_SMOKE_PASS id=${instance_id} pull_dir=${pull_dir}"
}

filter="gpu_name=RTX_4090 cuda_vers>=12.0 disk_space>=40 num_gpus=1 dph<=${max_dph} reliability>=0.95"

echo "search_filter=$filter"
offers=""
offer_deadline="$((SECONDS + offer_wait_seconds))"
while :; do
  offers="$(vastai search offers "$filter" -o 'dph' --raw)"
  if [[ -n "$offers" && "$offers" != "[]" ]]; then
    break
  fi
  if [[ "$offer_wait_seconds" == "0" || "$SECONDS" -ge "$offer_deadline" ]]; then
    break
  fi
  echo "NO_QUALIFYING_OFFER_YET waited_seconds=$((offer_wait_seconds - (offer_deadline - SECONDS)))"
  sleep "$offer_poll_seconds"
done
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
echo "LOCAL_SOURCE branch=${local_branch} head=${local_head} k_sq=${k_sq}"

one_shot_wait_ssh_seconds="$wait_ssh_seconds"
if [[ "$one_shot_wait_ssh_seconds" == "0" ]]; then
  one_shot_wait_ssh_seconds="600"
fi

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
  \$SSH_CMD "cd ${remote_dir} && tiles-maxxing/lb-source-propagation/scripts/remote_sidecar_smoke.sh --repo ${remote_dir} --k-sq ${k_sq} --out-dir /workspace/lb-source-remote-smoke"

PULL:
  mkdir -p ${pull_dir}
  rsync -avz -e "ssh -o StrictHostKeyChecking=accept-new -p \$PORT" root@\$HOST:/workspace/lb-source-remote-smoke/ ${pull_dir}/

ACCEPTANCE_CHECK:
  tiles-maxxing/lb-source-propagation/scripts/check_remote_smoke_artifacts.sh ${pull_dir} --expect-head ${local_head} --expect-branch ${local_branch} --expect-k-sq ${k_sq}

ONE_SHOT_REMOTE_SMOKE:
  $(shell_join "$0" --execute --run-remote-smoke --destroy-on-exit --max-dph "$max_dph" --max-budget "$max_budget" --k-sq "$k_sq" --offer-wait-seconds "$offer_wait_seconds" --offer-poll-seconds "$offer_poll_seconds" --wait-ssh-seconds "$one_shot_wait_ssh_seconds" --ssh-poll-seconds "$ssh_poll_seconds")
EOF

if [[ "$execute" -eq 0 ]]; then
  echo "DRY_RUN_ONLY"
  exit 0
fi

set +e
create_output="$("${create_cmd[@]}" 2>&1)"
create_status="$?"
set -e
printf '%s\n' "$create_output"
if [[ "$create_status" -ne 0 ]]; then
  exit "$create_status"
fi

created_instance_id="$(parse_created_instance_id "$create_output")"
if [[ -z "$created_instance_id" ]]; then
  echo "Could not parse created instance id from Vast output" >&2
  exit 5
fi
echo "CREATED_INSTANCE id=${created_instance_id}"

if [[ "$wait_ssh_seconds" != "0" ]]; then
  wait_for_ssh_ready "$created_instance_id"
fi

if [[ "$run_remote_smoke" -eq 1 ]]; then
  run_remote_smoke_gate "$created_instance_id"
  exit 0
fi

cat <<'EOF'
Instance creation requested. Wait for SSH readiness, then run the DEPLOY,
REMOTE_SMOKE, PULL, and ACCEPTANCE_CHECK commands printed above. This script
destroys instances only when --destroy-on-exit is supplied.
EOF
