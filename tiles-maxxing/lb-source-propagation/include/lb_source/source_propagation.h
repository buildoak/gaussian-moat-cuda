#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lb_source {

using AtomId = std::int64_t;

struct CarryAtom {
  AtomId id = 0;
  std::uint64_t norm_sq = 0;

  friend bool operator==(const CarryAtom&, const CarryAtom&) = default;
};

struct SeparatorState {
  std::vector<CarryAtom> carry_atoms;
  std::vector<std::vector<AtomId>> component_partition;
  std::vector<bool> source_bit_per_component;
  std::vector<std::vector<AtomId>> component_inventory;

  friend bool operator==(const SeparatorState&,
                         const SeparatorState&) = default;
};

struct BandAtom {
  AtomId id = 0;
  std::uint64_t norm_sq = 0;
  bool certified_source = false;
};

struct BandInput {
  std::uint64_t k_sq = 0;
  std::uint64_t outer_radius = 0;
  std::vector<BandAtom> atoms;
  std::vector<std::pair<AtomId, AtomId>> edges;
  bool force_overflow = false;
};

struct SourceSeed {
  std::string source_mode;
  std::string source_id;
  AtomId atom_id = 0;
};

struct SourceSeedApplyResult {
  std::size_t applied = 0;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

struct ProcessOptions {
  std::size_t max_atoms = 65535;
  std::size_t max_carry_atoms = 65535;
  std::size_t max_components = 65535;
};

enum class RejectReason {
  kNone,
  kOverflow,
  kMalformed,
};

struct ProcessResult {
  RejectReason reject = RejectReason::kNone;
  std::string diagnostic;
  std::uint64_t carry_width = 0;
  SeparatorState outgoing;
  bool terminal_source_dead = false;
  std::vector<AtomId> terminal_source_inventory;

  bool accepted() const noexcept { return reject == RejectReason::kNone; }
};

struct CarryManifest {
  std::uint64_t k_sq = 0;
  std::uint64_t outer_radius = 0;
  std::uint64_t carry_width = 0;
  SeparatorState separator;

  friend bool operator==(const CarryManifest&, const CarryManifest&) = default;
};

struct CarryManifestReadResult {
  CarryManifest manifest;
  std::string diagnostic;

  bool accepted() const noexcept { return diagnostic.empty(); }
};

struct SourceDraftMetadata {
  std::string source_mode;
  std::string source_id;
  std::string geometry_id;
  std::string commit_id;
  std::string build_id;
  std::string bz_status;
  std::string artifact_hash;

  friend bool operator==(const SourceDraftMetadata&,
                         const SourceDraftMetadata&) = default;
};

struct InventorySummary {
  std::uint64_t count = 0;
  std::string digest_algorithm;
  std::string digest_hex;

  friend bool operator==(const InventorySummary&,
                         const InventorySummary&) = default;
};

struct SourceProfileDraft {
  std::string profile_id;
  SourceDraftMetadata metadata;
  CarryManifest carry_manifest;
  RejectReason reject = RejectReason::kNone;
  std::string diagnostic;
  bool terminal_source_dead = false;
  std::vector<AtomId> terminal_source_inventory;
};

struct SourceCertificateDraft {
  std::string certificate_id;
  std::string profile_id;
  SourceDraftMetadata metadata;
  std::uint64_t k_sq = 0;
  std::uint64_t terminal_radius = 0;
  bool negative_guard_pass = false;
  std::vector<AtomId> terminal_source_inventory;
};

std::uint64_t ceil_sqrt(std::uint64_t n);

SeparatorState canonicalize_separator(const SeparatorState& state);

SourceSeedApplyResult apply_source_seeds(BandInput& band,
                                         const std::vector<SourceSeed>& seeds);

CarryManifest make_carry_manifest(std::uint64_t k_sq,
                                  std::uint64_t outer_radius,
                                  const ProcessResult& result);

std::ostream& write_carry_manifest(std::ostream& out,
                                   const CarryManifest& manifest);

CarryManifestReadResult read_carry_manifest(std::istream& in);

std::string carry_manifest_to_string(const CarryManifest& manifest);

CarryManifestReadResult carry_manifest_from_string(std::string_view text);

std::string source_profile_draft_json(const SourceProfileDraft& profile);

std::string source_certificate_draft_json(
    const SourceCertificateDraft& certificate);

InventorySummary summarize_inventory(const std::vector<AtomId>& atom_ids);

ProcessResult process_band(const BandInput& band,
                           const std::optional<SeparatorState>& incoming,
                           const ProcessOptions& options = {});

ProcessResult process_bands(const std::vector<BandInput>& bands,
                            const std::optional<SeparatorState>& incoming = {},
                            const ProcessOptions& options = {});

std::string reject_reason_name(RejectReason reason);

}  // namespace lb_source
