#!/usr/bin/env python3
"""Contract tests for the source propagation fixture schema guard."""

from __future__ import annotations

import json
from pathlib import Path
import unittest


VERIFICATION_ROOT = Path(__file__).resolve().parent
SCHEMA_PATH = VERIFICATION_ROOT / "schemas" / "source-prop-fixture.schema.json"
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


if __name__ == "__main__":
    raise SystemExit(unittest.main())
