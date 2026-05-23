#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  vast_sidecar_smoke_guard.sh [--execute] [--max-dph PRICE] [--max-budget USD]
                              [--k-sq N]
                              [--offer-id ID] [--remote DIR] [--pull-dir DIR]
                              [--exclude-offer-id ID]
                              [--exclude-host-id ID]
                              [--failure-ledger PATH]
                              [--offer-wait-seconds N]
                              [--offer-poll-seconds N]
                              [--max-create-attempts N]
                              [--wait-ssh-seconds N] [--ssh-poll-seconds N]
                              [--stop-on-ssh-timeout]
                              [--run-remote-smoke]
                              [--run-k26-timing-probe]
                              [--k26-timing-chunk-bands N]
                              [--k26-tileop-threads N]
                              [--destroy-on-exit]

Dry-run by default. Searches for a single RTX 4090 offer, enforces the price
cap, and prints the exact create/deploy/smoke/pull commands. With --execute it
creates the instance only by default. Add --run-remote-smoke to make the script
wait for SSH, deploy the current tree, run remote_sidecar_smoke.sh, pull
artifacts, and run the local artifact acceptance checker. Add
--run-k26-timing-probe instead to run remote_k26_timing_probe.sh and pull its
non-claim timing artifacts. Add --destroy-on-exit to destroy the created
instance after success or failure.

Optional offer polling handles transient market races, and optional SSH
readiness polling can stop or destroy a newly-created instance when SSH never
opens. Exclusion flags are for avoiding hosts/offers that have already failed
SSH readiness during the current campaign.
When --execute and --max-create-attempts is greater than one, SSH timeouts
destroy the timed-out instance when --destroy-on-exit is supplied, exclude that
offer id, and try the next capped offer.
When --failure-ledger is supplied, prior offer_id/host_id rows are loaded as
exclusions and new create/SSH failures are appended for future runs.

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
max_create_attempts="1"
wait_ssh_seconds="0"
ssh_poll_seconds="10"
stop_on_ssh_timeout=0
run_remote_smoke=0
run_k26_timing_probe=0
k26_timing_chunk_bands="1"
k26_tileop_threads="0"
destroy_on_exit=0
created_instance_id=""
failure_ledger=""
exclude_offer_ids=()
exclude_host_ids=()
ledger_extra_fields=""

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
    --exclude-offer-id)
      exclude_offer_ids+=("$2")
      shift 2
      ;;
    --exclude-host-id)
      exclude_host_ids+=("$2")
      shift 2
      ;;
    --failure-ledger)
      failure_ledger="$2"
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
    --max-create-attempts)
      max_create_attempts="$2"
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
    --run-k26-timing-probe)
      run_k26_timing_probe=1
      shift
      ;;
    --k26-timing-chunk-bands)
      k26_timing_chunk_bands="$2"
      shift 2
      ;;
    --k26-tileop-threads)
      k26_tileop_threads="$2"
      shift 2
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

if repo_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
  cd "$repo_root"
  local_head="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  local_branch="$(git branch --show-current 2>/dev/null || echo unknown)"
else
  repo_root="$PWD"
  local_head="${LB_SOURCE_LOCAL_HEAD:-unknown}"
  local_branch="${LB_SOURCE_LOCAL_BRANCH:-unknown}"
fi

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
require_nonnegative_integer "$max_create_attempts" "--max-create-attempts"
require_nonnegative_integer "$k26_tileop_threads" "--k26-tileop-threads"
require_nonnegative_integer "$k26_timing_chunk_bands" "--k26-timing-chunk-bands"
if [[ "$k26_timing_chunk_bands" == "0" ]]; then
  echo "--k26-timing-chunk-bands must be positive" >&2
  exit 2
fi
for id in "${exclude_offer_ids[@]+"${exclude_offer_ids[@]}"}"; do
  require_nonnegative_integer "$id" "--exclude-offer-id"
done
for id in "${exclude_host_ids[@]+"${exclude_host_ids[@]}"}"; do
  require_nonnegative_integer "$id" "--exclude-host-id"
done
if [[ "$wait_ssh_seconds" != "0" && "$ssh_poll_seconds" == "0" ]]; then
  echo "--ssh-poll-seconds must be positive when SSH readiness polling is enabled" >&2
  exit 2
fi
if [[ "$offer_wait_seconds" != "0" && "$offer_poll_seconds" == "0" ]]; then
  echo "--offer-poll-seconds must be positive when offer polling is enabled" >&2
  exit 2
fi
if [[ "$max_create_attempts" == "0" ]]; then
  echo "--max-create-attempts must be positive" >&2
  exit 2
fi
if [[ "$max_create_attempts" != "1" && -n "$offer_id" ]]; then
  echo "--max-create-attempts greater than 1 cannot be combined with --offer-id" >&2
  exit 2
fi
if [[ "$execute" -eq 1 && "$max_create_attempts" != "1" && "$destroy_on_exit" -ne 1 ]]; then
  echo "--max-create-attempts greater than 1 requires --destroy-on-exit when executing" >&2
  exit 2
fi
if [[ "$run_remote_smoke" -eq 1 && "$run_k26_timing_probe" -eq 1 ]]; then
  echo "--run-remote-smoke and --run-k26-timing-probe are mutually exclusive" >&2
  exit 2
fi
if [[ "$run_remote_smoke" -eq 1 && "$wait_ssh_seconds" == "0" ]]; then
  wait_ssh_seconds="600"
fi
if [[ "$run_k26_timing_probe" -eq 1 && "$wait_ssh_seconds" == "0" ]]; then
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

join_words() {
  local IFS=" "
  printf '%s\n' "$*"
}

append_unique_id() {
  local array_name="$1"
  local value="$2"
  local current
  [[ -z "$value" ]] && return
  case "$array_name" in
    exclude_offer_ids)
      for current in "${exclude_offer_ids[@]+"${exclude_offer_ids[@]}"}"; do
        [[ "$current" == "$value" ]] && return
      done
      exclude_offer_ids+=("$value")
      ;;
    exclude_host_ids)
      for current in "${exclude_host_ids[@]+"${exclude_host_ids[@]}"}"; do
        [[ "$current" == "$value" ]] && return
      done
      exclude_host_ids+=("$value")
      ;;
    *)
      echo "internal error: unknown exclusion array ${array_name}" >&2
      exit 2
      ;;
  esac
}

load_failure_ledger() {
  local line field offer host
  [[ -z "$failure_ledger" || ! -f "$failure_ledger" ]] && return
  while IFS= read -r line; do
    [[ -z "$line" || "$line" == \#* ]] && continue
    offer=""
    host=""
    for field in $line; do
      case "$field" in
        offer_id=*) offer="${field#offer_id=}" ;;
        host_id=*) host="${field#host_id=}" ;;
      esac
    done
    if [[ -n "$offer" && "$offer" != "unknown" ]]; then
      require_nonnegative_integer "$offer" "--failure-ledger offer_id"
      append_unique_id exclude_offer_ids "$offer"
    fi
    if [[ -n "$host" && "$host" != "unknown" ]]; then
      require_nonnegative_integer "$host" "--failure-ledger host_id"
      append_unique_id exclude_host_ids "$host"
    fi
  done < "$failure_ledger"
}

record_failure_ledger() {
  local reason="$1"
  [[ -z "$failure_ledger" ]] && return
  mkdir -p "$(dirname "$failure_ledger")"
  printf 'timestamp_utc=%s reason=%s offer_id=%s host_id=%s instance_id=%s branch=%s head=%s%s\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    "$reason" \
    "${selected_offer_id:-unknown}" \
    "${selected_host_id:-unknown}" \
    "${created_instance_id:-unknown}" \
    "$local_branch" \
    "$local_head" \
    "${ledger_extra_fields:+ ${ledger_extra_fields}}" >> "$failure_ledger"
  ledger_extra_fields=""
}

load_failure_ledger

exclude_offer_ids_joined="$(join_words "${exclude_offer_ids[@]+"${exclude_offer_ids[@]}"}")"
exclude_host_ids_joined="$(join_words "${exclude_host_ids[@]+"${exclude_host_ids[@]}"}")"

refresh_exclude_joins() {
  exclude_offer_ids_joined="$(join_words "${exclude_offer_ids[@]+"${exclude_offer_ids[@]}"}")"
  exclude_host_ids_joined="$(join_words "${exclude_host_ids[@]+"${exclude_host_ids[@]}"}")"
}

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

parse_create_success() {
  CREATE_OUTPUT="$1" python3 <<'PY'
import os
import re

text = os.environ.get("CREATE_OUTPUT", "")
match = re.search(r"['\"]success['\"]\s*:\s*(True|False|true|false)", text)
if match:
    print(match.group(1).lower())
PY
}

redact_create_output() {
  CREATE_OUTPUT="$1" python3 <<'PY'
import os
import re

text = os.environ.get("CREATE_OUTPUT", "")
text = re.sub(
    r"(['\"]instance_api_key['\"]\s*:\s*)['\"][^'\"]+['\"]",
    r"\1'<redacted>'",
    text,
)
print(text, end="" if text.endswith("\n") else "\n")
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
            "{}\t{}\t{}\t{}\t{}\t{}".format(
                item.get("cur_state") or "",
                item.get("intended_status") or "",
                item.get("actual_status") or "",
                item.get("ssh_host") or "",
                item.get("ssh_port") or "",
                str(item.get("status_msg") or "").replace("\n", " ")[:180],
            )
        )
        break
' "$instance_id"
}

wait_for_ssh_ready() {
  local instance_id="$1"
  local deadline="$((SECONDS + wait_ssh_seconds))"
  local state intended actual host port status_msg
  while (( SECONDS <= deadline )); do
    state=""
    intended=""
    actual=""
    host=""
    port=""
    status_msg=""
    IFS=$'\t' read -r state intended actual host port status_msg < <(instance_ssh_fields "$instance_id" || true) || true
    if [[ -n "${host:-}" && -n "${port:-}" ]]; then
      echo "SSH_PROBE id=${instance_id} state=${state:-unknown} intended=${intended:-unknown} actual=${actual:-unknown} host=${host} port=${port} status_msg=${status_msg:-none}"
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
  local state intended actual host port status_msg
  IFS=$'\t' read -r state intended actual host port status_msg < <(instance_ssh_fields "$instance_id" || true) || true
  if [[ -z "${host:-}" || -z "${port:-}" ]]; then
    echo "SSH endpoint unavailable for instance ${instance_id}" >&2
    exit 5
  fi
  printf '%s %s\n' "$host" "$port"
}

run_remote_smoke_gate() {
  local instance_id="$1"
  local host port ssh_cmd
  read -r host port < <(require_ssh_endpoint "$instance_id")
  ssh_cmd=(ssh -o StrictHostKeyChecking=accept-new -p "$port" "root@${host}")

  rm -rf "$pull_dir"
  mkdir -p "$pull_dir"

  echo "DEPLOYING_SOURCE id=${instance_id} host=${host} port=${port}"
  "${ssh_cmd[@]}" "rm -rf ${remote_dir} && mkdir -p ${remote_dir}"
  tar \
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
    -czf - . |
    "${ssh_cmd[@]}" "tar -xzf - -C ${remote_dir}"

  printf "deployed_local_head=%s\ndeployed_local_branch=%s\n" \
      "$local_head" "$local_branch" |
    "${ssh_cmd[@]}" \
      "mkdir -p /workspace/lb-source-remote-smoke && cat > /workspace/lb-source-remote-smoke/deployed_source.txt"

  echo "RUNNING_REMOTE_SMOKE id=${instance_id}"
  "${ssh_cmd[@]}" \
    "cd ${remote_dir} && tiles-maxxing/lb-source-propagation/scripts/remote_sidecar_smoke.sh --repo ${remote_dir} --k-sq ${k_sq} --out-dir /workspace/lb-source-remote-smoke"

  echo "PULLING_REMOTE_SMOKE_ARTIFACTS id=${instance_id}"
  "${ssh_cmd[@]}" "cd /workspace/lb-source-remote-smoke && tar -czf - ." |
    tar -xzf - -C "$pull_dir"

  echo "CHECKING_REMOTE_SMOKE_ARTIFACTS id=${instance_id}"
  tiles-maxxing/lb-source-propagation/scripts/check_remote_smoke_artifacts.sh \
    "$pull_dir" \
    --expect-head "$local_head" \
    --expect-branch "$local_branch" \
    --expect-k-sq "$k_sq"
  echo "REMOTE_VAST_SMOKE_PASS id=${instance_id} pull_dir=${pull_dir}"
}

run_remote_k26_timing_probe_gate() {
  local instance_id="$1"
  local host port ssh_cmd
  read -r host port < <(require_ssh_endpoint "$instance_id")
  ssh_cmd=(ssh -o StrictHostKeyChecking=accept-new -p "$port" "root@${host}")

  rm -rf "$pull_dir"
  mkdir -p "$pull_dir"

  echo "DEPLOYING_SOURCE id=${instance_id} host=${host} port=${port}"
  "${ssh_cmd[@]}" "rm -rf ${remote_dir} && mkdir -p ${remote_dir}"
  tar \
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
    -czf - . |
    "${ssh_cmd[@]}" "tar -xzf - -C ${remote_dir}"

  printf "deployed_local_head=%s\ndeployed_local_branch=%s\n" \
      "$local_head" "$local_branch" |
    "${ssh_cmd[@]}" \
      "mkdir -p /workspace/lb-source-k26-timing-probe && cat > /workspace/lb-source-k26-timing-probe/deployed_source.txt"

  echo "RUNNING_REMOTE_K26_TIMING_PROBE id=${instance_id}"
  "${ssh_cmd[@]}" \
    "cd ${remote_dir} && tiles-maxxing/lb-source-propagation/scripts/remote_k26_timing_probe.sh --repo ${remote_dir} --out-dir /workspace/lb-source-k26-timing-probe --chunk-bands ${k26_timing_chunk_bands} --tileop-threads ${k26_tileop_threads}"

  echo "PULLING_REMOTE_K26_TIMING_ARTIFACTS id=${instance_id}"
  "${ssh_cmd[@]}" "cd /workspace/lb-source-k26-timing-probe && tar -czf - ." |
    tar -xzf - -C "$pull_dir"

  echo "CHECKING_REMOTE_K26_TIMING_ARTIFACTS id=${instance_id}"
  tiles-maxxing/lb-source-propagation/scripts/check_remote_k26_timing_artifacts.sh \
    "$pull_dir" \
    --expect-head "$local_head" \
    --expect-branch "$local_branch"
  echo "REMOTE_K26_TIMING_PROBE_PASS id=${instance_id} pull_dir=${pull_dir}"
}

filter="gpu_name=RTX_4090 cuda_vers>=12.0 disk_space>=40 num_gpus=1 dph<=${max_dph} reliability>=0.95"
market_filter="gpu_name=RTX_4090 cuda_vers>=12.0 disk_space>=40 num_gpus=1 reliability>=0.95"
selected_offer_id=""
selected_offer_json=""
selected_offer_dph=""
selected_soft_hours=""
selected_host_id=""

add_failed_selection_exclusions() {
  append_unique_id exclude_offer_ids "$selected_offer_id"
  append_unique_id exclude_host_ids "$selected_host_id"
}

select_capped_offer() {
  refresh_exclude_joins
  offers=""
  offer_deadline="$((SECONDS + offer_wait_seconds))"
  while :; do
    offers="$(vastai search offers "$filter" -o 'dph' --raw)"
    offers="$(
      EXCLUDE_OFFER_IDS="$exclude_offer_ids_joined" \
      EXCLUDE_HOST_IDS="$exclude_host_ids_joined" \
      python3 -c '
import json
import os
import sys

offer_ids = {int(x) for x in os.environ.get("EXCLUDE_OFFER_IDS", "").split() if x}
host_ids = {int(x) for x in os.environ.get("EXCLUDE_HOST_IDS", "").split() if x}
data = json.load(sys.stdin)
filtered = [
    item for item in data
    if int(item.get("id", -1)) not in offer_ids
    and int(item.get("host_id", -1)) not in host_ids
]
print(json.dumps(filtered))
' <<<"$offers"
    )"
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
    set +e
    market_snapshot="$(
      vastai search offers "$market_filter" -o 'dph' --raw 2>/dev/null |
        EXCLUDE_OFFER_IDS="$exclude_offer_ids_joined" \
        EXCLUDE_HOST_IDS="$exclude_host_ids_joined" \
        MAX_DPH="$max_dph" \
        python3 -c '
import json
import os
import sys

offer_ids = {int(x) for x in os.environ.get("EXCLUDE_OFFER_IDS", "").split() if x}
host_ids = {int(x) for x in os.environ.get("EXCLUDE_HOST_IDS", "").split() if x}
cap = float(os.environ["MAX_DPH"])

try:
    data = json.load(sys.stdin)
except Exception:
    raise SystemExit(0)

filtered = [
    item for item in data
    if int(item.get("id", -1)) not in offer_ids
    and int(item.get("host_id", -1)) not in host_ids
    and item.get("dph_total") is not None
]
if not filtered:
    print("nearest_offer_id=unknown nearest_host_id=unknown nearest_dph=unknown nearest_over_cap_by=unknown")
    raise SystemExit(0)

nearest = min(filtered, key=lambda item: float(item["dph_total"]))
dph = float(nearest["dph_total"])
print(
    "nearest_offer_id={} nearest_host_id={} nearest_dph={:.4f} nearest_over_cap_by={:.4f}".format(
        nearest.get("id", "unknown"),
        nearest.get("host_id", "unknown"),
        dph,
        max(0.0, dph - cap),
    )
)
'
    )"
    market_snapshot_status="$?"
    set -e
    if [[ "$market_snapshot_status" == "0" && -n "$market_snapshot" ]]; then
      echo "NO_QUALIFYING_OFFER_MARKET ${market_snapshot}"
      ledger_extra_fields="$market_snapshot"
    fi
    record_failure_ledger "no_qualifying_offer"
    return 3
  fi

  selected_offer_id="$offer_id"
  if [[ -z "$selected_offer_id" ]]; then
    selected_offer_id="$(python3 -c 'import json,sys; data=json.load(sys.stdin); print(data[0]["id"])' <<<"$offers")"
  fi

  selected_offer_json="$(python3 -c 'import json,sys; oid=int(sys.argv[1]); data=json.load(sys.stdin); matches=[x for x in data if int(x["id"])==oid]; print(json.dumps(matches[0] if matches else {}))' "$selected_offer_id" <<<"$offers")"
  if [[ "$selected_offer_json" == "{}" ]]; then
    echo "offer_id ${selected_offer_id} is not present in the capped search results" >&2
    return 4
  fi

  selected_offer_dph="$(python3 -c 'import json,sys; print(json.load(sys.stdin)["dph_total"])' <<<"$selected_offer_json")"
  selected_host_id="$(python3 -c 'import json,sys; value=json.load(sys.stdin).get("host_id", ""); print("" if value is None else value)' <<<"$selected_offer_json")"
  python3 - "$selected_offer_dph" "$max_dph" <<'PY'
import sys
dph=float(sys.argv[1])
cap=float(sys.argv[2])
if not dph <= cap:
    raise SystemExit(f"offer price {dph} exceeds cap {cap}")
PY

  selected_soft_hours="$(python3 - "$max_budget" "$selected_offer_dph" <<'PY'
import sys
budget=float(sys.argv[1])
dph=float(sys.argv[2])
print(f"{budget / dph:.2f}")
PY
  )"
}

print_offer_plan() {
  refresh_exclude_joins
  echo "QUALIFYING_OFFER id=${selected_offer_id} host_id=${selected_host_id:-unknown} dph=${selected_offer_dph} budget_hours=${selected_soft_hours}"
  echo "LOCAL_SOURCE branch=${local_branch} head=${local_head} k_sq=${k_sq}"
  if [[ -n "$exclude_offer_ids_joined" ]]; then
    echo "EXCLUDED_OFFER_IDS ${exclude_offer_ids_joined}"
  fi
  if [[ -n "$exclude_host_ids_joined" ]]; then
    echo "EXCLUDED_HOST_IDS ${exclude_host_ids_joined}"
  fi
  if [[ -n "$failure_ledger" ]]; then
    echo "FAILURE_LEDGER ${failure_ledger}"
  fi

  one_shot_wait_ssh_seconds="$wait_ssh_seconds"
  if [[ "$one_shot_wait_ssh_seconds" == "0" ]]; then
    one_shot_wait_ssh_seconds="600"
  fi
  one_shot_cmd=("$0" --execute --destroy-on-exit
    --max-dph "$max_dph" --max-budget "$max_budget" --k-sq "$k_sq"
    --offer-wait-seconds "$offer_wait_seconds"
    --offer-poll-seconds "$offer_poll_seconds"
    --max-create-attempts "$max_create_attempts"
    --wait-ssh-seconds "$one_shot_wait_ssh_seconds"
    --ssh-poll-seconds "$ssh_poll_seconds")
  if [[ "$run_k26_timing_probe" -eq 1 ]]; then
    one_shot_cmd+=(--run-k26-timing-probe)
    one_shot_cmd+=(--k26-timing-chunk-bands "$k26_timing_chunk_bands")
    if [[ "$k26_tileop_threads" != "0" ]]; then
      one_shot_cmd+=(--k26-tileop-threads "$k26_tileop_threads")
    fi
  else
    one_shot_cmd+=(--run-remote-smoke)
  fi
  if [[ -n "$failure_ledger" ]]; then
    one_shot_cmd+=(--failure-ledger "$failure_ledger")
  fi
  for id in "${exclude_offer_ids[@]+"${exclude_offer_ids[@]}"}"; do
    one_shot_cmd+=(--exclude-offer-id "$id")
  done
  for id in "${exclude_host_ids[@]+"${exclude_host_ids[@]}"}"; do
    one_shot_cmd+=(--exclude-host-id "$id")
  done

  create_cmd=(vastai create instance "$selected_offer_id"
    --image pytorch/pytorch:2.5.1-cuda12.4-cudnn9-devel
    --disk 40 --ssh)

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
  \$SSH_CMD "rm -rf ${remote_dir} && mkdir -p ${remote_dir}"
  tar --exclude '.git' --exclude 'build*/' --exclude '**/build*/' --exclude '**/artifacts/' --exclude '**/results/' --exclude '**/profiles/' --exclude '**/runs/' --exclude '**/tmp/' --exclude '**/*.bin' --exclude '**/*.log' -czf - . | \$SSH_CMD "tar -xzf - -C ${remote_dir}"
EOF

  if [[ "$run_k26_timing_probe" -eq 1 ]]; then
    cat <<EOF
  printf "deployed_local_head=%s\ndeployed_local_branch=%s\n" "\$LOCAL_HEAD" "\$LOCAL_BRANCH" | \$SSH_CMD "mkdir -p /workspace/lb-source-k26-timing-probe && cat > /workspace/lb-source-k26-timing-probe/deployed_source.txt"

REMOTE_K26_TIMING_PROBE:
  \$SSH_CMD "cd ${remote_dir} && tiles-maxxing/lb-source-propagation/scripts/remote_k26_timing_probe.sh --repo ${remote_dir} --out-dir /workspace/lb-source-k26-timing-probe --chunk-bands ${k26_timing_chunk_bands} --tileop-threads ${k26_tileop_threads}"

PULL:
  mkdir -p ${pull_dir}
  \$SSH_CMD "cd /workspace/lb-source-k26-timing-probe && tar -czf - ." | tar -xzf - -C ${pull_dir}

ACCEPTANCE_CHECK:
  tiles-maxxing/lb-source-propagation/scripts/check_remote_k26_timing_artifacts.sh ${pull_dir} --expect-head ${local_head} --expect-branch ${local_branch}

ONE_SHOT_REMOTE_K26_TIMING_PROBE:
  $(shell_join "${one_shot_cmd[@]}")
EOF
  else
    cat <<EOF
  printf "deployed_local_head=%s\ndeployed_local_branch=%s\n" "\$LOCAL_HEAD" "\$LOCAL_BRANCH" | \$SSH_CMD "mkdir -p /workspace/lb-source-remote-smoke && cat > /workspace/lb-source-remote-smoke/deployed_source.txt"

REMOTE_SMOKE:
  \$SSH_CMD "cd ${remote_dir} && tiles-maxxing/lb-source-propagation/scripts/remote_sidecar_smoke.sh --repo ${remote_dir} --k-sq ${k_sq} --out-dir /workspace/lb-source-remote-smoke"

PULL:
  mkdir -p ${pull_dir}
  \$SSH_CMD "cd /workspace/lb-source-remote-smoke && tar -czf - ." | tar -xzf - -C ${pull_dir}

ACCEPTANCE_CHECK:
  tiles-maxxing/lb-source-propagation/scripts/check_remote_smoke_artifacts.sh ${pull_dir} --expect-head ${local_head} --expect-branch ${local_branch} --expect-k-sq ${k_sq}

ONE_SHOT_REMOTE_SMOKE:
  $(shell_join "${one_shot_cmd[@]}")
EOF
  fi
}

create_cmd=()

echo "search_filter=$filter"
attempt=1
while (( attempt <= max_create_attempts )); do
  set +e
  select_capped_offer
  select_status="$?"
  set -e
  if [[ "$select_status" -ne 0 ]]; then
    exit "$select_status"
  fi
  print_offer_plan

  if [[ "$execute" -eq 0 ]]; then
    echo "DRY_RUN_ONLY"
    exit 0
  fi

  set +e
  create_output="$("${create_cmd[@]}" 2>&1)"
  create_status="$?"
  set -e
  redact_create_output "$create_output"
  if [[ "$create_status" -ne 0 ]]; then
    exit "$create_status"
  fi

  created_instance_id="$(parse_created_instance_id "$create_output")"
  if [[ -z "$created_instance_id" ]]; then
    echo "Could not parse created instance id from Vast output" >&2
    exit 5
  fi
  echo "CREATED_INSTANCE id=${created_instance_id}"
  create_success="$(parse_create_success "$create_output")"
  if [[ "$create_success" == "false" ]]; then
    echo "CREATE_REPORTED_FAILURE id=${created_instance_id} offer_id=${selected_offer_id}" >&2
    if [[ "$destroy_on_exit" -eq 1 && -n "$created_instance_id" ]]; then
      echo "DESTROYING_FAILED_CREATE_INSTANCE id=${created_instance_id}" >&2
      vastai destroy instance "$created_instance_id" -y >&2 || true
    fi
    record_failure_ledger "create_reported_failure"
    created_instance_id=""
    if (( attempt < max_create_attempts )); then
      echo "RETRYING_AFTER_CREATE_FAILURE offer_id=${selected_offer_id} next_attempt=$((attempt + 1)) host_id=${selected_host_id:-unknown}"
      add_failed_selection_exclusions
      attempt=$((attempt + 1))
      continue
    fi
    exit 5
  fi

  if [[ "$wait_ssh_seconds" != "0" ]]; then
    set +e
    wait_for_ssh_ready "$created_instance_id"
    ssh_ready_status="$?"
    set -e
    if [[ "$ssh_ready_status" -ne 0 ]]; then
      if [[ "$destroy_on_exit" -eq 1 && -n "$created_instance_id" ]]; then
        echo "DESTROYING_UNREADY_INSTANCE id=${created_instance_id}" >&2
        vastai destroy instance "$created_instance_id" -y >&2 || true
      fi
      record_failure_ledger "ssh_timeout"
      created_instance_id=""
      if (( attempt < max_create_attempts )); then
        echo "RETRYING_AFTER_SSH_TIMEOUT offer_id=${selected_offer_id} next_attempt=$((attempt + 1)) host_id=${selected_host_id:-unknown}"
        add_failed_selection_exclusions
        attempt=$((attempt + 1))
        continue
      fi
      exit "$ssh_ready_status"
    fi
  fi

  if [[ "$run_remote_smoke" -eq 1 ]]; then
    run_remote_smoke_gate "$created_instance_id"
    exit 0
  fi
  if [[ "$run_k26_timing_probe" -eq 1 ]]; then
    run_remote_k26_timing_probe_gate "$created_instance_id"
    exit 0
  fi

  cat <<'EOF'
Instance creation requested. Wait for SSH readiness, then run the DEPLOY,
REMOTE_SMOKE, PULL, and ACCEPTANCE_CHECK commands printed above. This script
destroys instances only when --destroy-on-exit is supplied.
EOF
  exit 0
done
