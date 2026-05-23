#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  check_k26_full_run_bundle.sh OUT_DIR --source-dead-checker PATH
                                 --source-dead-gap-checker PATH

Validate a completed K26 source/origin bundle. This is stricter than the remote
smoke artifact checker: it expects the paid/full-run prefix result, strict
TileOp-port continuation result, repaired BZ schedule evidence, run profile,
run command contract, and source-dead certificate draft.

Required artifact names:
  k26_source_run_commands.json
  k26_bz_schedule_check.json
  k26_source_run_profile.json
  k26-prefix-result.json
  k26-prefix-progress.jsonl
  k26-continuation-result.json
  k26-continuation-progress.jsonl
  k26-continuation-chunks.jsonl, when present
  k26-prefix-manifest.txt
  k26-prefix-witness.txt
  k26-source-dead-gap.json
  k26-source-dead-cert.json
  k26-full-run-artifacts.sha256
USAGE
}

if [[ $# -lt 1 ]]; then
  usage >&2
  exit 2
fi

out_dir="$1"
shift
source_dead_gap_checker=""
source_dead_checker=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-dead-checker)
      source_dead_checker="$2"
      shift 2
      ;;
    --source-dead-gap-checker)
      source_dead_gap_checker="$2"
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

if [[ -z "$source_dead_checker" ]]; then
  echo "missing required --source-dead-checker" >&2
  exit 2
fi
if [[ -z "$source_dead_gap_checker" ]]; then
  echo "missing required --source-dead-gap-checker" >&2
  exit 2
fi
if [[ ! -x "$source_dead_checker" ]]; then
  echo "source-dead checker is not executable: $source_dead_checker" >&2
  exit 2
fi
if [[ ! -x "$source_dead_gap_checker" ]]; then
  echo "source-dead gap checker is not executable: $source_dead_gap_checker" >&2
  exit 2
fi
if [[ ! -d "$out_dir" ]]; then
  echo "bundle directory not found: $out_dir" >&2
  exit 1
fi

require_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "K26_FULL_RUN_BUNDLE_REJECT: missing required artifact: $path" >&2
    exit 1
  fi
}

require_grep() {
  local pattern="$1"
  local path="$2"
  local label="$3"
  if ! grep -Eq -- "$pattern" "$path"; then
    echo "K26_FULL_RUN_BUNDLE_REJECT: $label ($path)" >&2
    exit 1
  fi
}

json_string_value() {
  local path="$1"
  local field="$2"
  sed -nE "s/.*\"${field}\":\"([^\"]+)\".*/\\1/p" "$path" | head -n 1
}

json_object_string_value() {
  local path="$1"
  local object_field="$2"
  local field="$3"
  sed -nE "s/.*\"${object_field}\":\{[^}]*\"${field}\":\"([^\"]+)\"[^}]*\}.*/\\1/p" \
    "$path" | head -n 1
}

json_number_value() {
  local path="$1"
  local field="$2"
  sed -nE "s/.*\"${field}\":([0-9]+).*/\\1/p" "$path" | head -n 1
}

json_array_value() {
  local path="$1"
  local field="$2"
  sed -nE "s/.*\"${field}\":(\[[0-9, -]*\]).*/\\1/p" "$path" | head -n 1
}

atom_path_kind_counts() {
  local atom_path="$1"
  local body token coordinate_count=0 port_count=0
  body="${atom_path#[}"
  body="${body%]}"
  IFS=',' read -r -a tokens <<< "$body"
  for token in "${tokens[@]}"; do
    token="${token//[[:space:]]/}"
    [[ -z "$token" ]] && continue
    if [[ "$token" == -* ]]; then
      port_count=$((port_count + 1))
    else
      coordinate_count=$((coordinate_count + 1))
    fi
  done
  printf '%s %s\n' "$coordinate_count" "$port_count"
}

require_json_string_value() {
  local path="$1"
  local field="$2"
  local value
  value="$(json_string_value "$path" "$field")"
  if [[ -z "$value" ]]; then
    echo "K26_FULL_RUN_BUNDLE_REJECT: missing JSON string field ${field} ($path)" >&2
    exit 1
  fi
  printf '%s\n' "$value"
}

require_json_number_value() {
  local path="$1"
  local field="$2"
  local value
  value="$(json_number_value "$path" "$field")"
  if [[ -z "$value" ]]; then
    echo "K26_FULL_RUN_BUNDLE_REJECT: missing JSON number field ${field} ($path)" >&2
    exit 1
  fi
  printf '%s\n' "$value"
}

require_json_array_value() {
  local path="$1"
  local field="$2"
  local value
  value="$(json_array_value "$path" "$field")"
  if [[ -z "$value" ]]; then
    echo "K26_FULL_RUN_BUNDLE_REJECT: missing JSON array field ${field} ($path)" >&2
    exit 1
  fi
  printf '%s\n' "$value"
}

require_equal() {
  local expected="$1"
  local actual="$2"
  local label="$3"
  if [[ "$expected" != "$actual" ]]; then
    echo "K26_FULL_RUN_BUNDLE_REJECT: ${label}: expected ${expected}, got ${actual}" >&2
    exit 1
  fi
}

commands="$out_dir/k26_source_run_commands.json"
bz="$out_dir/k26_bz_schedule_check.json"
profile="$out_dir/k26_source_run_profile.json"
prefix="$out_dir/k26-prefix-result.json"
prefix_progress="$out_dir/k26-prefix-progress.jsonl"
continuation="$out_dir/k26-continuation-result.json"
continuation_progress="$out_dir/k26-continuation-progress.jsonl"
chunk_ledger="$out_dir/k26-continuation-chunks.jsonl"
prefix_manifest="$out_dir/k26-prefix-manifest.txt"
prefix_witness="$out_dir/k26-prefix-witness.txt"
gap="$out_dir/k26-source-dead-gap.json"
cert="$out_dir/k26-source-dead-cert.json"
artifact_manifest="$out_dir/k26-full-run-artifacts.sha256"

for artifact in "$commands" "$bz" "$profile" "$prefix" "$prefix_progress" \
    "$continuation" "$continuation_progress" "$prefix_manifest" \
    "$prefix_witness" "$gap" "$cert" "$artifact_manifest"; do
  require_file "$artifact"
done

require_manifest_hash() {
  local path="$1"
  local name="$2"
  local digest
  digest="$(shasum -a 256 "$path" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p')"
  if [[ -z "$digest" ]]; then
    echo "K26_FULL_RUN_BUNDLE_REJECT: could not hash artifact: $path" >&2
    exit 1
  fi
  if ! grep -Fxq "${digest}  ${name}" "$artifact_manifest"; then
    echo "K26_FULL_RUN_BUNDLE_REJECT: artifact hash mismatch for ${name}" >&2
    exit 1
  fi
}

require_manifest_hash "$commands" k26_source_run_commands.json
require_manifest_hash "$bz" k26_bz_schedule_check.json
require_manifest_hash "$profile" k26_source_run_profile.json
require_manifest_hash "$prefix" k26-prefix-result.json
require_manifest_hash "$prefix_progress" k26-prefix-progress.jsonl
require_manifest_hash "$continuation" k26-continuation-result.json
require_manifest_hash "$continuation_progress" k26-continuation-progress.jsonl
if [[ -f "$chunk_ledger" ]]; then
  require_manifest_hash "$chunk_ledger" k26-continuation-chunks.jsonl
fi
require_manifest_hash "$prefix_manifest" k26-prefix-manifest.txt
require_manifest_hash "$prefix_witness" k26-prefix-witness.txt
require_manifest_hash "$gap" k26-source-dead-gap.json
require_manifest_hash "$cert" k26-source-dead-cert.json

require_grep '"schema":"lb_source_k26_run_commands_v1"' "$commands" \
  "K26 command schema"
require_grep '"claim_label":"SOURCE_ORIGIN_K26"' "$commands" \
  "K26 command claim label"
require_grep '"executable_now":false' "$commands" \
  "K26 command contract must remain non-executable"
require_grep '"target":.*"tsuchimura_endpoint":.*"a":943460.*"b":376039.*"norm_sq":1031522101121' "$commands" \
  "K26 command Tsuchimura endpoint"
require_grep '"canonical_octant_endpoint":.*"a":376039.*"b":943460.*"norm_sq":1031522101121' "$commands" \
  "K26 command canonical endpoint"
require_grep '--endpoint-a 376039 --endpoint-b 943460' "$commands" \
  "K26 command prefix endpoint flags"
require_grep '"r_start":8192' "$commands" \
  "K26 command continuation start"
require_grep '"r_final":1015645' "$commands" \
  "K26 command continuation final radius"
require_grep '"schedule_boundary_count":124' "$commands" \
  "K26 command schedule boundary count"
require_grep '"schedule_segment_count":123' "$commands" \
  "K26 command schedule segment count"
require_grep '"schedule_min_width":8029' "$commands" \
  "K26 command schedule minimum width"
require_grep '"schedule_max_width":8193' "$commands" \
  "K26 command schedule maximum width"
require_grep '"schedule_radii_csv":"8192,.*122879,.*475135,.*622591,.*1015645"' "$commands" \
  "K26 command repaired schedule radii"
require_grep '"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1"' "$commands" \
  "K26 command BZ digest algorithm"
require_grep '"seam_bridge_policy":"diagnostic_allow_unbridged"' "$commands" \
  "K26 command diagnostic seam bridge policy"
require_grep '"blocked_if_unbridged_coordinate_carry_atoms":false' "$commands" \
  "K26 command permits proven dead-end unbridged carry"
require_grep '"claim_grade_requires_source_unbridged_unsafe_candidate_atoms":0' "$commands" \
  "K26 command source unsafe bridge stop condition"
require_grep '--target-a 376039 --target-b 943460' "$commands" \
  "K26 command target bridge flags"

require_grep '"schema":"lb_source_k26_bz_schedule_check_v1"' "$bz" \
  "K26 BZ schema"
require_grep '"proof_status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE"' "$bz" \
  "K26 BZ pass status"
require_grep '"accepted_for_schedule":true' "$bz" \
  "K26 BZ accepted-for-schedule flag"
require_grep '"accepted_for_claim":false' "$bz" \
  "K26 BZ must not be a source claim"
require_grep '"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1"' "$bz" \
  "K26 BZ digest algorithm"
require_grep '"repaired_summary":.*"bad_norm_count":0.*"bz_clean":true' "$bz" \
  "K26 repaired BZ clean summary"

require_grep '"schema":"lb_source_k26_run_profile_v1"' "$profile" \
  "K26 profile schema"
require_grep '"claim_label":"SOURCE_ORIGIN_K26"' "$profile" \
  "K26 profile claim label"
profile_id="$(require_json_string_value "$profile" profile_id)"
require_equal "k26-source-run-profile" "$profile_id" \
  "K26 profile id"
require_grep '"profile_status":"RUN_PROFILE_DRAFT_NON_CLAIM"' "$profile" \
  "K26 profile status"
require_grep '"target":.*"tsuchimura_endpoint":.*"a":943460.*"b":376039.*"norm_sq":1031522101121' "$profile" \
  "K26 profile Tsuchimura endpoint"
require_grep '"canonical_octant_endpoint":.*"a":376039.*"b":943460.*"norm_sq":1031522101121' "$profile" \
  "K26 profile canonical endpoint"
require_grep '"expected_component_size":14542615005' "$profile" \
  "K26 profile Tsuchimura component size"
require_grep '"terminal_radius":1015645' "$profile" \
  "K26 profile terminal radius"
require_grep '"preferred_band_width":8192' "$profile" \
  "K26 profile preferred band width"
require_grep '"band_count":124' "$profile" \
  "K26 profile band count"
require_grep '"repaired_boundary_count":3' "$profile" \
  "K26 profile repaired boundary count"
require_grep '"max_abs_boundary_shift":1' "$profile" \
  "K26 profile max boundary shift"
require_grep '"nominal_dirty_row_indices":\[15,58,75\]' "$profile" \
  "K26 profile dirty BZ row indices"
require_grep '"prefix_row_index":0' "$profile" \
  "K26 profile prefix row"
require_grep '"tileop_port_first_row_index":1' "$profile" \
  "K26 profile TileOp continuation row"
require_grep '"accepted_for_schedule":true' "$profile" \
  "K26 profile BZ schedule binding"
require_grep '"accepted_for_claim":false' "$profile" \
  "K26 profile BZ non-claim binding"

bz_digest="$(require_json_string_value "$bz" schedule_digest_hex)"
commands_digest="$(require_json_string_value "$commands" schedule_digest_hex)"
profile_digest="$(require_json_string_value "$profile" schedule_digest_hex)"
require_equal "$bz_digest" "$commands_digest" "K26 BZ digest command binding"
require_equal "$bz_digest" "$profile_digest" "K26 BZ digest profile binding"

validate_chunk_ledger() {
  if [[ ! -f "$chunk_ledger" ]]; then
    return
  fi
  python3 - "$out_dir" "$commands" "$continuation" "$artifact_manifest" <<'PY'
import hashlib
import json
import pathlib
import sys

out_dir = pathlib.Path(sys.argv[1])
commands_path = pathlib.Path(sys.argv[2])
continuation_path = pathlib.Path(sys.argv[3])
artifact_manifest_path = pathlib.Path(sys.argv[4])
ledger_path = out_dir / "k26-continuation-chunks.jsonl"
prefix_manifest_name = "k26-prefix-manifest.txt"


def reject(message: str) -> None:
    print(
        f"K26_FULL_RUN_BUNDLE_REJECT: K26 chunk ledger {message}",
        file=sys.stderr,
    )
    raise SystemExit(1)


def load_json(path: pathlib.Path) -> dict:
    try:
        return json.loads(path.read_text())
    except Exception as exc:  # noqa: BLE001
        reject(f"bad JSON in {path.name}: {exc}")


def require_file(name: str, label: str) -> pathlib.Path:
    if not name:
        reject(f"missing {label}")
    path = out_dir / name
    if not path.is_file():
        reject(f"{label} does not exist: {name}")
    return path


def digest(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


manifest_entries = set()
for raw in artifact_manifest_path.read_text().splitlines():
    parts = raw.split()
    if len(parts) == 2:
        manifest_entries.add((parts[0], parts[1]))


def require_hashed(name: str, label: str) -> pathlib.Path:
    path = require_file(name, label)
    entry = (digest(path), name)
    if entry not in manifest_entries:
        reject(f"{label} hash is absent or stale in k26-full-run-artifacts.sha256: {name}")
    return path


commands = load_json(commands_path)
continuation = load_json(continuation_path)
radii_csv = commands.get("continuation", {}).get("schedule_radii_csv")
if not isinstance(radii_csv, str) or not radii_csv:
    reject("missing command schedule_radii_csv")
try:
    radii = [int(part) for part in radii_csv.split(",")]
except ValueError:
    reject("command schedule_radii_csv is not integer CSV")
if len(radii) < 2:
    reject("command schedule has fewer than two radii")
segment_count = len(radii) - 1

rows = []
for line_no, line in enumerate(ledger_path.read_text().splitlines(), start=1):
    if not line.strip():
        continue
    try:
        rows.append(json.loads(line))
    except json.JSONDecodeError as exc:
        reject(f"line {line_no} is not JSON: {exc}")
if not rows:
    reject("is empty")

expected_start = 0
previous_output = prefix_manifest_name
for index, row in enumerate(rows):
    if row.get("schema") != "lb_source_k26_continuation_chunk_v1":
        reject(f"row {index} has wrong schema")
    if row.get("chunk_index") != index:
        reject(f"row {index} has nonsequential chunk_index")
    if row.get("chunk_id") != f"{index:03d}":
        reject(f"row {index} has unexpected chunk_id")
    if row.get("action") not in {"executed", "reused"}:
        reject(f"row {index} has invalid action")
    start = row.get("schedule_segment_start")
    end = row.get("schedule_segment_end")
    count = row.get("schedule_segment_count")
    if not isinstance(start, int) or not isinstance(end, int) or not isinstance(count, int):
        reject(f"row {index} has noninteger schedule segment fields")
    if start != expected_start:
        reject(f"row {index} starts at {start}, expected {expected_start}")
    if end <= start or end > segment_count:
        reject(f"row {index} has invalid segment interval")
    if count != end - start:
        reject(f"row {index} has wrong segment count")
    expected_csv = ",".join(str(value) for value in radii[start : end + 1])
    if row.get("schedule_radii_csv") != expected_csv:
        reject(f"row {index} schedule_radii_csv does not match command schedule")
    if row.get("r_start") != radii[start] or row.get("r_final") != radii[end]:
        reject(f"row {index} radius endpoints do not match command schedule")
    final_chunk = end == segment_count
    if row.get("final_chunk") is not final_chunk:
        reject(f"row {index} final_chunk flag mismatch")
    if row.get("input_manifest") != previous_output:
        reject(f"row {index} input_manifest does not chain from previous output")
    require_hashed(row.get("input_manifest", ""), f"row {index} input_manifest")
    result_path = require_hashed(row.get("result", ""), f"row {index} result")
    require_hashed(row.get("progress", ""), f"row {index} progress")
    output_manifest = row.get("output_manifest")
    if final_chunk:
        if output_manifest != "":
            reject(f"row {index} final chunk unexpectedly has output_manifest")
        if result_path.read_bytes() != continuation_path.read_bytes():
            reject(f"row {index} final result differs from k26-continuation-result.json")
        if row.get("terminal_source_dead") is not True:
            reject(f"row {index} final chunk must record terminal_source_dead=true")
        if row.get("has_source_carry") is not False:
            reject(f"row {index} final chunk must record has_source_carry=false")
    else:
        output_path = require_hashed(output_manifest, f"row {index} output_manifest")
        previous_output = output_path.name
        if row.get("terminal_source_dead") is not False:
            reject(f"row {index} non-final chunk must record terminal_source_dead=false")
        if row.get("has_source_carry") is not True:
            reject(f"row {index} non-final chunk must record has_source_carry=true")
        source_carry_atoms = row.get("source_carry_atoms")
        if not isinstance(source_carry_atoms, int) or source_carry_atoms <= 0:
            reject(f"row {index} live chunk must record positive source_carry_atoms")
    expected_start = end

if expected_start != segment_count:
    reject(f"does not cover all command schedule segments: stopped at {expected_start}")
if rows[-1].get("r_final") != continuation.get("r_final"):
    reject("final row r_final does not match continuation result")
PY
}

validate_chunk_ledger

require_grep '"schema":"lb_source_origin_cpu_runner_v1"' "$prefix" \
  "K26 prefix result schema"
require_grep '"proof_status":"DIAGNOSTIC_NON_CLAIM"' "$prefix" \
  "K26 prefix non-claim status"
require_grep '"k_sq":26' "$prefix" "K26 prefix k_sq"
require_grep '"r_final":8192' "$prefix" "K26 prefix outer radius"
require_grep '"accepted":true' "$prefix" "K26 prefix accepted"
require_grep '"terminal_source_dead":false' "$prefix" \
  "K26 prefix must have live source carry"
require_grep '"has_source_carry":true' "$prefix" \
  "K26 prefix source carry"
require_grep '"manifest_written":true' "$prefix" \
  "K26 prefix manifest"
require_grep '"prefix_witness_written":true' "$prefix" \
  "K26 prefix witness"

require_grep '"schema":"lb_source_tileop_port_runner_v1"' "$continuation" \
  "K26 continuation result schema"
require_grep '"proof_status":"DIAGNOSTIC_NON_CLAIM"' "$continuation" \
  "K26 continuation non-claim status"
require_grep '"source_mode":"ORIGIN_PREFIX_PORT_WITNESS"' "$continuation" \
  "K26 continuation source mode"
require_grep '"seam_bridge_policy":"diagnostic_allow_unbridged"' "$continuation" \
  "K26 continuation diagnostic seam bridge policy"
require_grep '"k_sq":26' "$continuation" "K26 continuation k_sq"
require_grep '"r_start":8192' "$continuation" "K26 continuation start"
require_grep '"r_final":1015645' "$continuation" "K26 continuation final radius"
require_grep '"schedule_mode":"explicit_radii"' "$continuation" \
  "K26 continuation schedule mode"
require_grep '"schedule_boundary_count":124' "$continuation" \
  "K26 continuation schedule boundary count"
require_grep '"tileop_overflows":0' "$continuation" \
  "K26 continuation overflow-free"
require_grep '"source_unbridged_unsafe_candidate_atoms":0' "$continuation" \
  "K26 continuation source unsafe bridge"
require_grep '"target":\{"enabled":true,"a":376039,"b":943460,"norm_sq":1031522101121,"seen":true.*"source_reached":true' "$continuation" \
  "K26 continuation target source reachability"
require_grep '"path_provenance":"mixed_coordinate_port_atom_chain_non_claim"' "$continuation" \
  "K26 continuation target atom-chain provenance"
require_grep '"atom_path_length":[1-9][0-9]*' "$continuation" \
  "K26 continuation target atom-chain length"
require_grep '"atom_path":\[[0-9,-]+' "$continuation" \
  "K26 continuation target atom-chain"
require_grep '"accepted":true' "$continuation" \
  "K26 continuation accepted"
require_grep '"terminal_source_dead":true' "$continuation" \
  "K26 continuation terminal source death"
require_grep '"has_source_carry":false' "$continuation" \
  "K26 continuation no live source carry"
require_grep '"source_inventory_count":14542615005' "$continuation" \
  "K26 continuation Tsuchimura component size"
require_grep '"source_inventory_digest_algorithm":"sha256:lb_source_inventory_v1"' "$continuation" \
  "K26 continuation inventory digest algorithm"
require_grep '"source_inventory_digest_hex":"[0-9a-f]{64}"' "$continuation" \
  "K26 continuation inventory digest"
require_grep '"max_source_norm_sq":[1-9][0-9]*' "$continuation" \
  "K26 continuation max source norm"
require_grep '"max_source_norm_atom_ids":\[[0-9]' "$continuation" \
  "K26 continuation max-norm tie set"

require_grep '"schema":"lb_source_k26_source_dead_gap_v1"' "$gap" \
  "K26 source-dead gap schema"
require_grep '"proof_status":"DIAGNOSTIC_NON_CLAIM"' "$gap" \
  "K26 source-dead gap non-claim status"
require_grep '"blocker":"SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING"' "$gap" \
  "K26 source-dead gap blocker"
require_grep '"continuation_artifact":.*"name":"k26-continuation-result.json".*"sha256":"[0-9a-f]{64}"' "$gap" \
  "K26 source-dead gap continuation binding"
require_grep '"bz_evidence":.*"status":"BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE".*"accepted_for_schedule":true.*"accepted_for_claim":false' "$gap" \
  "K26 source-dead gap BZ evidence flags"
require_grep '"bz_evidence":.*"schedule_digest_algorithm":"sha256:lb_source_k26_repaired_bz_schedule_v1".*"schedule_digest_hex":"[0-9a-f]{64}"' "$gap" \
  "K26 source-dead gap BZ digest"
require_grep '"bridge_safety":.*"seam_bridge_policy":"diagnostic_allow_unbridged"' "$gap" \
  "K26 source-dead gap bridge policy"
require_grep '"bridge_safety":.*"source_unbridged_unsafe_candidate_atoms":0' "$gap" \
  "K26 source-dead gap unsafe bridge stop condition"
require_grep '"target_path_provenance":"mixed_coordinate_port_atom_chain_non_claim"' "$gap" \
  "K26 source-dead gap target path provenance"
require_grep '"coordinate_path_obligation":.*"required_provenance":"coordinate_gaussian_prime_path".*"observed_provenance":"mixed_coordinate_port_atom_chain_non_claim".*"per_port_coordinate_expansion":"missing".*"claim_grade_path_accepted":false' "$gap" \
  "K26 source-dead gap coordinate path obligation"
require_grep '"terminal_inventory_obligation":.*"required_mode":"claim_grade_terminal_inventory".*"observed_mode":"summary_digest_only_non_claim".*"listed_inventory_present":false.*"claim_grade_inventory_accepted":false' "$gap" \
  "K26 source-dead gap terminal inventory obligation"
require_grep '"missing_for_source_dead_cert":.*coordinate Gaussian-prime source_path' "$gap" \
  "K26 source-dead gap missing coordinate source path"
require_grep '"missing_for_source_dead_cert":.*terminal inventory' "$gap" \
  "K26 source-dead gap missing terminal inventory"
require_grep '"missing_for_source_dead_cert":.*BZ schedule' "$gap" \
  "K26 source-dead gap missing BZ schedule binding"
require_grep '"missing_for_source_dead_cert":.*verifier' "$gap" \
  "K26 source-dead gap missing verifier binding"

actual_continuation_digest="$(
  shasum -a 256 "$continuation" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p'
)"
gap_continuation_digest="$(
  json_object_string_value "$gap" continuation_artifact sha256
)"
if [[ -z "$gap_continuation_digest" ]]; then
  echo "K26_FULL_RUN_BUNDLE_REJECT: missing JSON string field continuation_artifact.sha256 ($gap)" >&2
  exit 1
fi
require_equal "$actual_continuation_digest" "$gap_continuation_digest" \
  "K26 gap continuation hash binding"
if [[ -f "$chunk_ledger" ]]; then
  require_grep '"chunk_ledger_artifact":.*"name":"k26-continuation-chunks.jsonl".*"sha256":"[0-9a-f]{64}"' \
    "$gap" "K26 source-dead gap chunk ledger binding"
  actual_chunk_ledger_digest="$(
    shasum -a 256 "$chunk_ledger" | sed -nE 's/^([0-9a-f]{64}) .*/\1/p'
  )"
  gap_chunk_ledger_name="$(
    json_object_string_value "$gap" chunk_ledger_artifact name
  )"
  gap_chunk_ledger_digest="$(
    json_object_string_value "$gap" chunk_ledger_artifact sha256
  )"
  require_equal "k26-continuation-chunks.jsonl" "$gap_chunk_ledger_name" \
    "K26 gap chunk ledger artifact name binding"
  require_equal "$actual_chunk_ledger_digest" "$gap_chunk_ledger_digest" \
    "K26 gap chunk ledger hash binding"
fi
gap_bz_digest="$(require_json_string_value "$gap" schedule_digest_hex)"
require_equal "$bz_digest" "$gap_bz_digest" \
  "K26 gap BZ digest binding"
continuation_bridge_policy="$(
  require_json_string_value "$continuation" seam_bridge_policy
)"
gap_bridge_policy="$(require_json_string_value "$gap" seam_bridge_policy)"
require_equal "$continuation_bridge_policy" "$gap_bridge_policy" \
  "K26 gap bridge policy binding"
for bridge_field in \
    source_bridged_coordinate_carry_atoms \
    source_unbridged_coordinate_carry_atoms \
    source_unbridged_without_next_band_candidates \
    source_unbridged_with_next_band_candidates \
    source_unbridged_dead_end_candidate_atoms \
    source_unbridged_unsafe_candidate_atoms \
    source_bridge_rejected_candidate_atoms; do
  continuation_bridge_value="$(require_json_number_value "$continuation" "$bridge_field")"
  gap_bridge_value="$(require_json_number_value "$gap" "$bridge_field")"
  require_equal "$continuation_bridge_value" "$gap_bridge_value" \
    "K26 gap bridge ${bridge_field} binding"
done
continuation_path_provenance="$(
  require_json_string_value "$continuation" path_provenance
)"
gap_path_provenance="$(require_json_string_value "$gap" target_path_provenance)"
continuation_atom_path_length="$(
  require_json_number_value "$continuation" atom_path_length
)"
gap_atom_path_length="$(require_json_number_value "$gap" target_atom_path_length)"
continuation_atom_path="$(require_json_array_value "$continuation" atom_path)"
gap_atom_path="$(require_json_array_value "$gap" target_atom_path)"
require_equal "$continuation_path_provenance" "$gap_path_provenance" \
  "K26 gap path provenance binding"
require_equal "$continuation_atom_path_length" "$gap_atom_path_length" \
  "K26 gap atom path length binding"
require_equal "$continuation_atom_path" "$gap_atom_path" \
  "K26 gap atom path binding"
gap_observed_provenance="$(require_json_string_value "$gap" observed_provenance)"
require_equal "$gap_path_provenance" "$gap_observed_provenance" \
  "K26 gap coordinate path observed provenance binding"
atom_path_counts="$(atom_path_kind_counts "$gap_atom_path")"
gap_coordinate_atom_count="$(
  require_json_number_value "$gap" observed_coordinate_atom_count
)"
gap_port_atom_count="$(require_json_number_value "$gap" observed_port_atom_count)"
require_equal "${atom_path_counts%% *}" "$gap_coordinate_atom_count" \
  "K26 gap coordinate path coordinate atom count"
require_equal "${atom_path_counts##* }" "$gap_port_atom_count" \
  "K26 gap coordinate path port atom count"
continuation_inventory_count="$(
  require_json_number_value "$continuation" source_inventory_count
)"
continuation_inventory_digest="$(
  require_json_string_value "$continuation" source_inventory_digest_hex
)"
continuation_max_norm="$(require_json_number_value "$continuation" max_source_norm_sq)"
continuation_max_ties="$(
  require_json_array_value "$continuation" max_source_norm_atom_ids
)"
gap_inventory_count="$(require_json_number_value "$gap" count)"
gap_inventory_digest="$(require_json_string_value "$gap" digest_hex)"
gap_max_norm="$(require_json_number_value "$gap" max_norm_sq)"
gap_max_ties="$(require_json_array_value "$gap" max_norm_atom_ids)"
require_equal "$continuation_inventory_count" "$gap_inventory_count" \
  "K26 gap inventory count binding"
require_equal "$continuation_inventory_digest" "$gap_inventory_digest" \
  "K26 gap inventory digest binding"
require_equal "$continuation_max_norm" "$gap_max_norm" \
  "K26 gap max source norm binding"
require_equal "$continuation_max_ties" "$gap_max_ties" \
  "K26 gap max-norm tie binding"
gap_observed_inventory_count="$(
  require_json_number_value "$gap" observed_count
)"
gap_observed_inventory_digest="$(
  require_json_string_value "$gap" observed_digest_hex
)"
gap_observed_max_norm="$(
  require_json_number_value "$gap" observed_max_norm_sq
)"
require_equal "$continuation_inventory_count" "$gap_observed_inventory_count" \
  "K26 gap terminal inventory obligation count binding"
require_equal "$continuation_inventory_digest" "$gap_observed_inventory_digest" \
  "K26 gap terminal inventory obligation digest binding"
require_equal "$continuation_max_norm" "$gap_observed_max_norm" \
  "K26 gap terminal inventory obligation max-norm binding"

gap_checker_output="$("$source_dead_gap_checker" "$gap")"
if ! grep -q '"status":"SOURCE_DEAD_GAP_NON_CLAIM_PASS"' \
    <<<"$gap_checker_output"; then
  echo "K26_FULL_RUN_BUNDLE_REJECT: source-dead gap checker did not accept gap artifact" >&2
  echo "$gap_checker_output" >&2
  exit 1
fi

require_grep '"schema":"lb_source_dead_cert_draft_v1"' "$cert" \
  "K26 source-dead cert schema"
require_grep '"certificate_id":"[^"]+"' "$cert" \
  "K26 source-dead cert certificate id"
cert_profile_id="$(require_json_string_value "$cert" profile_id)"
require_equal "$profile_id" "$cert_profile_id" \
  "K26 source-dead cert profile binding"
cert_source_mode="$(require_json_string_value "$cert" source_mode)"
cert_geometry_id="$(require_json_string_value "$cert" geometry_id)"
cert_bz_status="$(require_json_string_value "$cert" bz_status)"
cert_artifact_hash="$(require_json_string_value "$cert" artifact_hash)"
require_equal "ORIGIN_SOURCE" "$cert_source_mode" \
  "K26 source-dead cert source mode"
require_equal "SOURCE_ORIGIN_K26" "$cert_geometry_id" \
  "K26 source-dead cert geometry binding"
require_equal "BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE" "$cert_bz_status" \
  "K26 source-dead cert BZ status binding"
if ! grep -Eq '"artifact_hash":"sha256:[0-9a-f]{64}"' "$cert"; then
  echo "K26_FULL_RUN_BUNDLE_REJECT: K26 source-dead cert artifact hash binding ($cert)" >&2
  exit 1
fi
require_equal "sha256:${actual_continuation_digest}" "$cert_artifact_hash" \
  "K26 source-dead cert continuation artifact hash binding"
require_grep '"k_sq":26' "$cert" "K26 source-dead cert k_sq"
require_grep '"terminal_radius":1015645' "$cert" \
  "K26 source-dead cert terminal radius"
require_grep '"negative_guard_pass":true' "$cert" \
  "K26 source-dead cert negative guard"
require_grep '"endpoint":.*"a":376039.*"b":943460.*"norm_sq":1031522101121' "$cert" \
  "K26 source-dead cert canonical endpoint"
require_grep '"endpoint_atom_id":1615075207964004' "$cert" \
  "K26 source-dead cert endpoint atom id"
require_grep '"source_path_provenance":"coordinate_gaussian_prime_path"' "$cert" \
  "K26 source-dead cert coordinate path provenance"
require_grep '"source_path":\[' "$cert" \
  "K26 source-dead cert source path"
require_grep '"terminal_source_inventory_summary":.*"count":14542615005' "$cert" \
  "K26 source-dead cert Tsuchimura component size"
require_grep '"digest_algorithm":"sha256:lb_source_inventory_v1"' "$cert" \
  "K26 source-dead cert inventory digest algorithm"
require_grep '"digest_hex":"[0-9a-f]{64}"' "$cert" \
  "K26 source-dead cert inventory digest"
require_grep '"max_norm_sq":[1-9][0-9]*' "$cert" \
  "K26 source-dead cert max norm"
require_grep '"max_norm_atom_ids":\[[0-9]' "$cert" \
  "K26 source-dead cert max-norm tie set"

continuation_inventory_count="$(require_json_number_value "$continuation" source_inventory_count)"
cert_inventory_count="$(require_json_number_value "$cert" count)"
continuation_inventory_digest="$(require_json_string_value "$continuation" source_inventory_digest_hex)"
cert_inventory_digest="$(require_json_string_value "$cert" digest_hex)"
continuation_max_norm="$(require_json_number_value "$continuation" max_source_norm_sq)"
cert_max_norm="$(require_json_number_value "$cert" max_norm_sq)"
continuation_max_ties="$(require_json_array_value "$continuation" max_source_norm_atom_ids)"
cert_max_ties="$(require_json_array_value "$cert" max_norm_atom_ids)"
require_equal "$continuation_inventory_count" "$cert_inventory_count" \
  "K26 cert inventory count binding"
require_equal "$continuation_inventory_digest" "$cert_inventory_digest" \
  "K26 cert inventory digest binding"
require_equal "$continuation_max_norm" "$cert_max_norm" \
  "K26 cert max source norm binding"
require_equal "$continuation_max_ties" "$cert_max_ties" \
  "K26 cert max-norm tie binding"

if checker_output="$("$source_dead_checker" "$cert" 2>&1)"; then
  if grep -q '"status":"SOURCE_DEAD_CERT_DRAFT_PASS"' <<<"$checker_output"; then
    echo '{"status":"K26_FULL_RUN_BUNDLE_DRAFT_PASS"}'
  elif grep -q '"status":"SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM_PASS"' <<<"$checker_output"; then
    echo '{"status":"K26_FULL_RUN_BUNDLE_BLOCKED_SOURCE_DEAD_CERT_SUMMARY_ONLY_NON_CLAIM"}'
    exit 3
  else
    echo "K26_FULL_RUN_BUNDLE_REJECT: source-dead checker did not accept draft cert" >&2
    echo "$checker_output" >&2
    exit 1
  fi
else
  echo "K26_FULL_RUN_BUNDLE_REJECT: source-dead checker did not accept draft cert" >&2
  echo "$checker_output" >&2
  exit 1
fi
