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
    def test_schema_pins_composed_band_count_guard_to_five_through_twenty_bands(
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
        self.assertEqual(band_rule["maxItems"], 20)

    def test_composed_band_count_guard_fixtures_cover_lower_and_upper_gate(
        self,
    ) -> None:
        expected_counts = {
            "composed-vs-big-equivalence.json": 5,
            "composed-ten-vs-big-equivalence.json": 10,
            "composed-twenty-vs-big-equivalence.json": 20,
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
        reached_path_obligation = reached_rule["properties"][
            "coordinate_path_obligation"
        ]["properties"]
        self.assertTrue(
            reached_path_obligation[
                "origin_prefix_witness_path_available"
            ]["const"]
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
        target_not_reached_path = target_not_reached_rule["properties"][
            "coordinate_path_obligation"
        ]["properties"]
        self.assertFalse(
            target_not_reached_path[
                "origin_prefix_witness_path_available"
            ]["const"]
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

    def test_source_dead_gap_schema_allows_chunk_bridge_source_binding(self) -> None:
        schema = load_json(DEAD_GAP_SCHEMA_PATH)
        self.assertNotIn("bridge_source_artifact", schema["required"])
        bridge_source = schema["properties"]["bridge_source_artifact"]
        self.assertEqual(
            bridge_source["properties"]["name"]["const"],
            "k26-continuation-chunk-000.json",
        )
        self.assertEqual(
            bridge_source["properties"]["sha256"]["pattern"],
            "^[0-9a-f]{64}$",
        )

        bridge_safety = schema["properties"]["bridge_safety"]
        self.assertIn(
            "source_coordinate_carry_atoms_with_next_band_candidates",
            bridge_safety["required"],
        )

    def test_source_dead_gap_schema_requires_bz_schedule_obligation(self) -> None:
        schema = load_json(DEAD_GAP_SCHEMA_PATH)
        self.assertIn("bz_schedule_obligation", schema["required"])
        obligation = schema["properties"]["bz_schedule_obligation"]
        self.assertEqual(
            obligation["properties"]["required_status"]["const"],
            "claim_grade_bz_schedule",
        )
        self.assertEqual(
            obligation["properties"]["observed_status"]["const"],
            "BZ_REPAIRED_SCHEDULE_PASS_NON_SOURCE",
        )
        self.assertFalse(
            obligation["properties"]["claim_grade_bz_accepted"]["const"],
        )

    def test_source_dead_gap_schema_allows_non_claim_accumulator_binding(self) -> None:
        schema = load_json(DEAD_GAP_SCHEMA_PATH)
        self.assertNotIn("terminal_source_inventory_accumulator", schema["required"])
        accumulator = schema["properties"]["terminal_source_inventory_accumulator"]
        self.assertEqual(
            accumulator["properties"]["mode"]["const"],
            "summary_digest_only_non_claim",
        )
        self.assertEqual(
            accumulator["properties"]["provenance"]["const"],
            "terminal_component_inventory_accumulator",
        )
        self.assertEqual(
            accumulator["properties"]["claim_grade_inventory_accepted"]["const"],
            False,
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

    def test_source_dead_cert_schema_keeps_accumulator_mode_conditionals(
        self,
    ) -> None:
        schema = load_json(DEAD_CERT_SCHEMA_PATH)
        self.assertEqual(
            set(schema["properties"]["terminal_source_inventory_mode"]["enum"]),
            {"listed", "summary_only_non_claim", "claim_grade_accumulator"},
        )

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
            "terminal_source_inventory_accumulator",
            summary_rule["then"]["required"],
        )
        summary_accumulator = summary_rule["then"]["properties"][
            "terminal_source_inventory_accumulator"
        ]["properties"]
        self.assertEqual(
            summary_accumulator["mode"]["const"],
            "summary_digest_only_non_claim",
        )
        self.assertFalse(
            summary_accumulator["claim_grade_inventory_accepted"]["const"],
        )
        self.assertIn(
            "terminal_source_inventory",
            summary_rule["then"]["not"]["required"],
        )

        claim_rules = [
            rule
            for rule in schema["allOf"]
            if rule.get("if", {})
            .get("properties", {})
            .get("terminal_source_inventory_mode", {})
            .get("const")
            == "claim_grade_accumulator"
        ]
        self.assertEqual(len(claim_rules), 1)
        claim_rule = claim_rules[0]
        self.assertIn(
            "terminal_source_inventory_accumulator",
            claim_rule["then"]["required"],
        )
        claim_accumulator = claim_rule["then"]["properties"][
            "terminal_source_inventory_accumulator"
        ]["properties"]
        self.assertEqual(
            claim_accumulator["mode"]["const"],
            "claim_grade_digest_accumulator",
        )
        self.assertTrue(
            claim_accumulator["claim_grade_inventory_accepted"]["const"],
        )
        claim_forbidden = claim_rule["then"]["not"]["anyOf"]
        self.assertIn({"required": ["proof_status"]}, claim_forbidden)
        self.assertIn({"required": ["non_claim"]}, claim_forbidden)
        self.assertIn({"required": ["terminal_source_inventory"]}, claim_forbidden)

        listed_rules = [
            rule
            for rule in schema["allOf"]
            if "not" in rule.get("if", {})
            and "terminal_source_inventory_mode"
            in rule["if"]["not"].get("properties", {})
        ]
        self.assertEqual(len(listed_rules), 1)
        listed_rule = listed_rules[0]
        self.assertIn("terminal_source_inventory", listed_rule["then"]["required"])
        listed_forbidden = listed_rule["then"]["not"]["anyOf"]
        self.assertIn({"required": ["proof_status"]}, listed_forbidden)
        self.assertIn({"required": ["non_claim"]}, listed_forbidden)
        self.assertIn(
            {"required": ["terminal_source_inventory_accumulator"]},
            listed_forbidden,
        )

        accumulator = schema["properties"]["terminal_source_inventory_accumulator"]
        self.assertEqual(
            set(accumulator["properties"]["mode"]["enum"]),
            {"summary_digest_only_non_claim", "claim_grade_digest_accumulator"},
        )
        claim_shapes = [
            shape
            for shape in accumulator["oneOf"]
            if shape["properties"]["mode"]["const"]
            == "claim_grade_digest_accumulator"
        ]
        self.assertEqual(len(claim_shapes), 1)
        claim_shape = claim_shapes[0]
        for field in (
            "accumulator_algorithm",
            "complete_stream_observed",
            "canonical_order",
            "duplicate_free",
            "retired_component_finalized",
            "overflow_checked",
        ):
            self.assertIn(field, claim_shape["required"])
        self.assertTrue(
            claim_shape["properties"]["complete_stream_observed"]["const"],
        )
        self.assertTrue(
            claim_shape["properties"]["claim_grade_inventory_accepted"]["const"],
        )


if __name__ == "__main__":
    raise SystemExit(unittest.main())
