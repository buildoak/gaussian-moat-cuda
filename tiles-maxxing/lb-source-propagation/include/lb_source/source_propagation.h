#pragma once

#include <cstdint>
#include <optional>
#include <string>
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

std::uint64_t ceil_sqrt(std::uint64_t n);

SeparatorState canonicalize_separator(const SeparatorState& state);

ProcessResult process_band(const BandInput& band,
                           const std::optional<SeparatorState>& incoming,
                           const ProcessOptions& options = {});

ProcessResult process_bands(const std::vector<BandInput>& bands,
                            const std::optional<SeparatorState>& incoming = {},
                            const ProcessOptions& options = {});

std::string reject_reason_name(RejectReason reason);

}  // namespace lb_source
