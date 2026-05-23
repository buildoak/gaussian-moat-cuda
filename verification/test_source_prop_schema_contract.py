#!/usr/bin/env python3
"""Contract tests for the source propagation fixture schema guard."""

from __future__ import annotations

import json
from pathlib import Path
import unittest


VERIFICATION_ROOT = Path(__file__).resolve().parent
SCHEMA_PATH = VERIFICATION_ROOT / "schemas" / "source-prop-fixture.schema.json"
DEAD_GAP_SCHEMA_PATH = (
    VERIFICATION_ROOT / "schemas" / "source-dead-gap.schema.json"
)
DEAD_CERT_SCHEMA_PATH = (
    VERIFICATION_ROOT / "schemas" / "source-dead-cert-draft.schema.json"
)
FIXTURE_ROOT = VERIFICATION_ROOT / "fixtures" / "source_prop"
BAND_COUNT_GUARD = "composed_band_count_guard"


def load_json(path: Path) -> dict:
    data = json.loads(path.read_text())
    if not isinstance(data, dict):
        raise TypeError(f"{path} did not parse to a JSON object")
    return data


class SourcePropSchemaContractTest(unittest.TestCase):
    def test_schema_pins_composed_band_count_guard_to_five_through_ten_bands(
        self,
    ) -> None:
        schema = load_json(SCHEMA_PATH)
        guards = schema["properties"]["guards"]["items"]["enum"]
        self.assertIn(BAND_COUNT_GUARD, guards)

        conditionals = schema["allOf"]
        matching_rules = [
            rule
            for rule in conditionals
            if rule.get("if", {})
            .get("properties", {})
            .get("guards", {})
            .get("contains", {})
            .get("const")
            == BAND_COUNT_GUARD
        ]
        self.assertEqual(len(matching_rules), 1)

        band_rule = matching_rules[0]["then"]["properties"]["bands"]
        self.assertEqual(band_rule["minItems"], 5)
        self.assertEqual(band_rule["maxItems"], 10)

    def test_composed_band_count_guard_fixtures_cover_lower_and_upper_gate(
        self,
    ) -> None:
        expected_counts = {
            "composed-vs-big-equivalence.json": 5,
            "composed-ten-vs-big-equivalence.json": 10,
        }
        for name, band_count in expected_counts.items():
            with self.subTest(fixture=name):
                fixture = load_json(FIXTURE_ROOT / name)
                self.assertIn(BAND_COUNT_GUARD, fixture["guards"])
                self.assertEqual(len(fixture["bands"]), band_count)

    def test_bad_composed_band_count_fixture_exercises_schema_rejection_shape(
        self,
    ) -> None:
        fixture = load_json(FIXTURE_ROOT / "bad-composed-band-count-too-small.json")
        self.assertIn(BAND_COUNT_GUARD, fixture["guards"])
        self.assertLess(len(fixture["bands"]), 5)

    def test_short_adversarial_fixtures_do_not_claim_band_count_guard(self) -> None:
        short_composed_fixtures = (
            "false-weld.json",
            "k32-carry-width-minimum.json",
            "non-source-partition-merge.json",
            "one-band-identity.json",
            "source-only-carry-loss.json",
        )
        for name in short_composed_fixtures:
            with self.subTest(fixture=name):
                fixture = load_json(FIXTURE_ROOT / name)
                self.assertIn("composed_equals_big", fixture["guards"])
                self.assertNotIn(BAND_COUNT_GUARD, fixture["guards"])
                self.assertLess(len(fixture["bands"]), 5)

    def test_source_dead_gap_schema_covers_both_diagnostic_blockers(self) -> None:
        schema = load_json(DEAD_GAP_SCHEMA_PATH)
        blockers = set(schema["properties"]["blocker"]["enum"])
        self.assertEqual(
            blockers,
            {
                "SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING",
                "SOURCE_DEAD_CERT_TARGET_NOT_REACHED",
            },
        )

        rules_by_blocker = {
            rule["if"]["properties"]["blocker"]["const"]: rule["then"]
            for rule in schema["allOf"]
            if "blocker" in rule.get("if", {}).get("properties", {})
        }
        self.assertEqual(set(rules_by_blocker), blockers)

        reached_rule = rules_by_blocker["SOURCE_DEAD_CERT_COORDINATE_PATH_MISSING"]
        self.assertEqual(
            reached_rule["properties"]["target_path_provenance"]["const"],
            "mixed_coordinate_port_atom_chain_non_claim",
        )
        self.assertEqual(
            reached_rule["properties"]["terminal_source_inventory_summary"][
                "properties"
            ]["count"]["const"],
            14542615005,
        )

        target_not_reached_rule = rules_by_blocker[
            "SOURCE_DEAD_CERT_TARGET_NOT_REACHED"
        ]
        self.assertEqual(
            target_not_reached_rule["properties"]["target_path_provenance"][
                "const"
            ],
            "component_reachability_only",
        )
        self.assertEqual(
            target_not_reached_rule["properties"]["target_atom_path_length"][
                "const"
            ],
            0,
        )
        self.assertEqual(
            target_not_reached_rule["properties"]["target_atom_path"]["maxItems"],
            0,
        )
        self.assertEqual(
            target_not_reached_rule["properties"][
                "terminal_source_inventory_summary"
            ]["properties"]["max_norm_sq"]["maximum"],
            1031522101120,
        )

    def test_source_dead_gap_schema_matches_target_not_reached_fixture_shape(
        self,
    ) -> None:
        schema = load_json(DEAD_GAP_SCHEMA_PATH)
        fixture = load_json(FIXTURE_ROOT / "dead-gap-target-not-reached-valid.json")
        self.assertEqual(fixture["blocker"], "SOURCE_DEAD_CERT_TARGET_NOT_REACHED")
        self.assertIn(
            fixture["blocker"],
            schema["properties"]["blocker"]["enum"],
        )
        self.assertEqual(fixture["target_path_provenance"], "component_reachability_only")
        self.assertEqual(fixture["target_atom_path_length"], 0)
        self.assertEqual(fixture["target_atom_path"], [])
        self.assertLess(
            fixture["terminal_source_inventory_summary"]["max_norm_sq"],
            1031522101121,
        )

    def test_source_dead_gap_schema_allows_chunk_ledger_artifact_binding(self) -> None:
        schema = load_json(DEAD_GAP_SCHEMA_PATH)
        self.assertNotIn("chunk_ledger_artifact", schema["required"])
        chunk_ledger = schema["properties"]["chunk_ledger_artifact"]
        self.assertEqual(
            chunk_ledger["properties"]["name"]["const"],
            "k26-continuation-chunks.jsonl",
        )
        self.assertEqual(
            chunk_ledger["properties"]["sha256"]["pattern"],
            "^[0-9a-f]{64}$",
        )

    def test_source_dead_cert_schema_pins_hash_shape_and_k26_endpoint(self) -> None:
        schema = load_json(DEAD_CERT_SCHEMA_PATH)
        self.assertEqual(
            schema["properties"]["metadata"]["properties"]["artifact_hash"][
                "pattern"
            ],
            "^sha256:[0-9a-f]{64}$",
        )

        k26_rules = [
            rule["then"]
            for rule in schema["allOf"]
            if rule.get("if", {})
            .get("properties", {})
            .get("metadata", {})
            .get("properties", {})
            .get("geometry_id", {})
            .get("const")
            == "SOURCE_ORIGIN_K26"
        ]
        self.assertEqual(len(k26_rules), 1)
        k26_rule = k26_rules[0]["properties"]
        self.assertEqual(k26_rule["k_sq"]["const"], 26)
        self.assertEqual(k26_rule["terminal_radius"]["const"], 1015645)
        self.assertEqual(k26_rule["endpoint"]["properties"]["a"]["const"], 376039)
        self.assertEqual(k26_rule["endpoint"]["properties"]["b"]["const"], 943460)
        self.assertEqual(
            k26_rule["endpoint"]["properties"]["norm_sq"]["const"],
            1031522101121,
        )
        self.assertEqual(
            k26_rule["metadata"]["properties"]["source_mode"]["const"],
            "ORIGIN_SOURCE",
        )
        self.assertEqual(
            k26_rule["metadata"]["properties"]["bz_status"]["const"],
            "BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE",
        )
        self.assertEqual(k26_rule["endpoint_atom_id"]["const"], 1615075207964004)
        self.assertEqual(k26_rule["source_path"]["minItems"], 2)
        self.assertEqual(
            k26_rule["source_path"]["prefixItems"][0]["properties"]["norm_sq"][
                "maximum"
            ],
            26,
        )

        source_path_target = k26_rule["source_path"]["contains"]["properties"]
        self.assertEqual(source_path_target["a"]["const"], 376039)
        self.assertEqual(source_path_target["b"]["const"], 943460)
        self.assertEqual(source_path_target["norm_sq"]["const"], 1031522101121)

    def test_source_dead_cert_schema_keeps_summary_only_non_claim_conditional(
        self,
    ) -> None:
        schema = load_json(DEAD_CERT_SCHEMA_PATH)
        summary_rules = [
            rule
            for rule in schema["allOf"]
            if rule.get("if", {})
            .get("properties", {})
            .get("terminal_source_inventory_mode", {})
            .get("const")
            == "summary_only_non_claim"
        ]
        self.assertEqual(len(summary_rules), 1)
        summary_rule = summary_rules[0]
        self.assertEqual(
            summary_rule["then"]["properties"]["proof_status"]["const"],
            "SUMMARY_ONLY_NON_CLAIM",
        )
        self.assertIn(
            "terminal_source_inventory",
            summary_rule["then"]["not"]["required"],
        )
        self.assertIn("terminal_source_inventory", summary_rule["else"]["required"])
        listed_forbidden = summary_rule["else"]["not"]["anyOf"]
        self.assertIn({"required": ["proof_status"]}, listed_forbidden)
        self.assertIn({"required": ["non_claim"]}, listed_forbidden)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
