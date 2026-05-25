#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  vast_high_radius_cuda_campaign.sh [--execute] [--destroy-on-exit]
                                    [--max-dph PRICE] [--max-budget USD]
                                    [--pull-dir DIR]
                                    [--max-create-attempts N]
                                    [--wait-ssh-seconds N]
                                    [--ssh-poll-seconds N]

Rent one RTX 4090 Vast instance, deploy the current tree, run the high-radius
CUDA campaign, pull artifacts, and optionally destroy the instance on exit.
Dry-run by default.
USAGE
}

execute=0
destroy_on_exit=0
max_dph="0.75"
max_budget="1.50"
pull_dir="tiles-maxxing/cuda-campaign-v2-sqrt-36/artifacts/high-radius-cuda"
max_create_attempts="5"
wait_ssh_seconds="600"
ssh_poll_seconds="10"
remote_dir="/workspace/gaussian-moat-cuda"
remote_out="/workspace/high-radius-cuda-campaign"
created_instance_id=""
exclude_offer_ids=()
exclude_host_ids=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --execute)
      execute=1
      shift
      ;;
    --destroy-on-exit)
      destroy_on_exit=1
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
    --pull-dir)
      pull_dir="$2"
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

require_nonnegative_integer() {
  local value="$1"
  local label="$2"
  if ! [[ "$value" =~ ^[0-9]+$ ]]; then
    echo "${label} must be a nonnegative integer: ${value}" >&2
    exit 2
  fi
}

require_nonnegative_integer "$max_create_attempts" "--max-create-attempts"
require_nonnegative_integer "$wait_ssh_seconds" "--wait-ssh-seconds"
require_nonnegative_integer "$ssh_poll_seconds" "--ssh-poll-seconds"
if [[ "$max_create_attempts" == "0" ]]; then
  echo "--max-create-attempts must be positive" >&2
  exit 2
fi

if ! command -v vastai >/dev/null 2>&1; then
  echo "vastai CLI not found" >&2
  exit 2
fi

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"
local_head="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
local_branch="$(git branch --show-current 2>/dev/null || echo unknown)"

cleanup_instance() {
  local status="$?"
  if [[ "$destroy_on_exit" -eq 1 && -n "$created_instance_id" ]]; then
    echo "DESTROYING_INSTANCE_ON_EXIT id=${created_instance_id}" >&2
    vastai destroy instance "$created_instance_id" -y >&2 || true
  fi
  return "$status"
}
trap cleanup_instance EXIT

shell_join() {
  local out="" arg
  for arg in "$@"; do
    printf -v arg '%q' "$arg"
    out+="${out:+ }${arg}"
  done
  printf '%s\n' "$out"
}

join_words() {
  local IFS=" "
  printf '%s\n' "$*"
}

select_offer() {
  local filter offers exclude_offer exclude_host
  filter="gpu_name=RTX_4090 cuda_vers>=12.0 disk_space>=40 num_gpus=1 dph<=${max_dph} reliability>=0.95"
  echo "search_filter=${filter}" >&2
  offers="$(vastai search offers "$filter" -o dph --raw)"
  exclude_offer="$(join_words "${exclude_offer_ids[@]+"${exclude_offer_ids[@]}"}")"
  exclude_host="$(join_words "${exclude_host_ids[@]+"${exclude_host_ids[@]}"}")"
  OFFERS="$offers" EXCLUDE_OFFER_IDS="$exclude_offer" \
      EXCLUDE_HOST_IDS="$exclude_host" python3 <<'PY'
import json
import os

offer_ids = {int(x) for x in os.environ.get("EXCLUDE_OFFER_IDS", "").split() if x}
host_ids = {int(x) for x in os.environ.get("EXCLUDE_HOST_IDS", "").split() if x}
data = json.loads(os.environ["OFFERS"])
rows = [
    row for row in data
    if int(row.get("id", -1)) not in offer_ids
    and int(row.get("host_id", -1)) not in host_ids
]
if not rows:
    raise SystemExit(3)
rows.sort(key=lambda row: float(row.get("dph_total") or 999.0))
row = rows[0]
print(json.dumps({
    "id": row["id"],
    "host_id": row.get("host_id"),
    "dph_total": row.get("dph_total"),
}))
PY
}

parse_created_instance_id() {
  CREATE_OUTPUT="$1" python3 <<'PY'
import os
import re

match = re.search(r"['\"]new_contract['\"]\s*:\s*([0-9]+)",
                  os.environ.get("CREATE_OUTPUT", ""))
if match:
    print(match.group(1))
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
  local raw status
  set +e
  raw="$(vastai show instances --raw 2>/dev/null)"
  status="$?"
  set -e
  if [[ "$status" -ne 0 || -z "$raw" ]]; then
    return 0
  fi
  INSTANCES="$raw" python3 - "$instance_id" <<'PY'
import json
import os
import sys

instance_id = int(sys.argv[1])
try:
    data = json.loads(os.environ["INSTANCES"])
except Exception:
    raise SystemExit(0)
for item in data:
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
PY
}

wait_for_ssh_ready() {
  local instance_id="$1"
  local deadline="$((SECONDS + wait_ssh_seconds))"
  local state intended actual host port status_msg
  while (( SECONDS <= deadline )); do
    state=""; intended=""; actual=""; host=""; port=""; status_msg=""
    IFS=$'\t' read -r state intended actual host port status_msg < <(
      instance_ssh_fields "$instance_id" || true
    ) || true
    if [[ -n "$host" && -n "$port" ]]; then
      echo "SSH_PROBE id=${instance_id} state=${state:-unknown} actual=${actual:-unknown} host=${host} port=${port} status_msg=${status_msg:-none}"
      if ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
          -o ConnectTimeout=8 -p "$port" "root@${host}" \
          'echo SSH_READY && hostname' >/dev/null; then
        echo "SSH_READY id=${instance_id} host=${host} port=${port}"
        printf '%s %s\n' "$host" "$port"
        return 0
      fi
    else
      echo "SSH_PROBE id=${instance_id} metadata_unavailable"
    fi
    sleep "$ssh_poll_seconds"
  done
  echo "SSH_TIMEOUT id=${instance_id} waited_seconds=${wait_ssh_seconds}" >&2
  return 5
}

run_campaign_on_instance() {
  local instance_id="$1"
  local host="$2"
  local port="$3"
  local ssh_cmd=(ssh -o StrictHostKeyChecking=accept-new -p "$port" "root@${host}")

  rm -rf "$pull_dir"
  mkdir -p "$pull_dir"

  echo "DEPLOYING_SOURCE id=${instance_id} host=${host} port=${port}"
  "${ssh_cmd[@]}" "rm -rf ${remote_dir} ${remote_out} && mkdir -p ${remote_dir} ${remote_out}"
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
    "${ssh_cmd[@]}" "cat > ${remote_out}/deployed_source.txt"

  echo "RUNNING_HIGH_RADIUS_CUDA_CAMPAIGN id=${instance_id}"
  "${ssh_cmd[@]}" \
    "cd ${remote_dir} && tiles-maxxing/cuda-campaign-v2-sqrt-36/scripts/remote_high_radius_cuda_campaign.sh --repo ${remote_dir} --out-dir ${remote_out} --k-sq 36 --radii 60000000,80000000 --chunk-size 200000 --timeout-seconds 1200"

  echo "PULLING_HIGH_RADIUS_CUDA_ARTIFACTS id=${instance_id}"
  "${ssh_cmd[@]}" "cd ${remote_out} && tar -czf - ." |
    tar -xzf - -C "$pull_dir"

  python3 - "$pull_dir" <<'PY'
import json
import pathlib
import sys

summary = json.loads((pathlib.Path(sys.argv[1]) / "summary.json").read_text())
print(
    f"HIGH_RADIUS_CUDA_CAMPAIGN_LOCAL_SUMMARY "
    f"{summary['status']} rows={len(summary['rows'])}"
)
for comp in summary.get("comparisons", []):
    print(
        "COMPARE "
        f"anchor={comp['anchor']} "
        f"four_by_8192_total={comp['four_by_8192_total_seconds']:.3f} "
        f"one_by_32768_total={float(comp['one_by_32768_total_seconds']):.3f} "
        f"ratio={comp['total_ratio_four_over_wide']:.3f}"
    )
PY
  echo "HIGH_RADIUS_CUDA_CAMPAIGN_PULL_PASS pull_dir=${pull_dir}"
}

echo "LOCAL_SOURCE branch=${local_branch} head=${local_head}"
for attempt in $(seq 1 "$max_create_attempts"); do
  offer_json="$(select_offer)"
  offer_id="$(python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])' <<<"$offer_json")"
  host_id="$(python3 -c 'import json,sys; print(json.load(sys.stdin).get("host_id") or "")' <<<"$offer_json")"
  offer_dph="$(python3 -c 'import json,sys; print(json.load(sys.stdin).get("dph_total"))' <<<"$offer_json")"
  budget_hours="$(python3 - "$max_budget" "$offer_dph" <<'PY'
import sys
print(f"{float(sys.argv[1]) / float(sys.argv[2]):.2f}")
PY
)"
  echo "QUALIFYING_OFFER attempt=${attempt} id=${offer_id} host_id=${host_id:-unknown} dph=${offer_dph} budget_hours=${budget_hours}"

  create_cmd=(vastai create instance "$offer_id"
    --image pytorch/pytorch:2.5.1-cuda12.4-cudnn9-devel
    --disk 40 --ssh)
  echo "CREATE: $(shell_join "${create_cmd[@]}")"
  if [[ "$execute" -eq 0 ]]; then
    echo "DRY_RUN_ONLY pull_dir=${pull_dir}"
    exit 0
  fi

  set +e
  create_output="$("${create_cmd[@]}" 2>&1)"
  create_status="$?"
  set -e
  redact_create_output "$create_output"
  if [[ "$create_status" -ne 0 ]]; then
    exclude_offer_ids+=("$offer_id")
    [[ -n "$host_id" ]] && exclude_host_ids+=("$host_id")
    continue
  fi

  created_instance_id="$(parse_created_instance_id "$create_output")"
  if [[ -z "$created_instance_id" ]]; then
    echo "Could not parse created instance id" >&2
    exclude_offer_ids+=("$offer_id")
    [[ -n "$host_id" ]] && exclude_host_ids+=("$host_id")
    continue
  fi
  echo "CREATED_INSTANCE id=${created_instance_id}"

  set +e
  endpoint="$(wait_for_ssh_ready "$created_instance_id")"
  ssh_ready_status="$?"
  set -e
  if [[ "$ssh_ready_status" -ne 0 ]]; then
    if [[ "$destroy_on_exit" -eq 1 ]]; then
      echo "DESTROYING_UNREADY_INSTANCE id=${created_instance_id}" >&2
      vastai destroy instance "$created_instance_id" -y >&2 || true
    fi
    created_instance_id=""
    exclude_offer_ids+=("$offer_id")
    [[ -n "$host_id" ]] && exclude_host_ids+=("$host_id")
    continue
  fi
  printf '%s\n' "$endpoint"
  read -r host port <<<"$(printf '%s\n' "$endpoint" | tail -n 1)"
  run_campaign_on_instance "$created_instance_id" "$host" "$port"
  exit 0
done

echo "HIGH_RADIUS_CUDA_CAMPAIGN_NO_USABLE_INSTANCE" >&2
exit 5
