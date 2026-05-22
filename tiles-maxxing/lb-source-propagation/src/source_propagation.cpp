#include "lb_source/source_propagation.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace lb_source {
namespace {

class Dsu {
 public:
  explicit Dsu(std::size_t n) : parent_(n), rank_(n, 0) {
    for (std::size_t i = 0; i < n; ++i) {
      parent_[i] = i;
    }
  }

  std::size_t find(std::size_t x) {
    assert(x < parent_.size());
    if (parent_[x] != x) {
      parent_[x] = find(parent_[x]);
    }
    return parent_[x];
  }

  void unite(std::size_t a, std::size_t b) {
    std::size_t ra = find(a);
    std::size_t rb = find(b);
    if (ra == rb) {
      return;
    }
    if (rank_[ra] < rank_[rb] || (rank_[ra] == rank_[rb] && rb < ra)) {
      std::swap(ra, rb);
    }
    parent_[rb] = ra;
    if (rank_[ra] == rank_[rb]) {
      ++rank_[ra];
    }
  }

 private:
  std::vector<std::size_t> parent_;
  std::vector<std::uint8_t> rank_;
};

ProcessResult reject(RejectReason reason, std::string diagnostic,
                     std::uint64_t carry_width = 0) {
  ProcessResult result;
  result.reject = reason;
  result.diagnostic = std::move(diagnostic);
  result.carry_width = carry_width;
  return result;
}

bool lexicographic_component_less(const std::vector<AtomId>& a,
                                  const std::vector<AtomId>& b) {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

bool in_final_carry_window(std::uint64_t norm_sq, std::uint64_t outer_radius,
                           std::uint64_t carry_width) {
  if (norm_sq > outer_radius * outer_radius) {
    return false;
  }
  const std::uint64_t inner_radius =
      outer_radius > carry_width ? outer_radius - carry_width : 0;
  return norm_sq >= inner_radius * inner_radius;
}

bool square_ge(std::uint64_t x, std::uint64_t n) {
  if (n == 0) {
    return true;
  }
  if (x == 0) {
    return false;
  }
  const std::uint64_t q = n / x;
  const std::uint64_t r = n % x;
  return x > q || (x == q && r == 0);
}

}  // namespace

std::uint64_t ceil_sqrt(std::uint64_t n) {
  if (n == 0) {
    return 0;
  }
  std::uint64_t lo = 0;
  std::uint64_t hi = 1;
  while (!square_ge(hi, n)) {
    if (hi > std::numeric_limits<std::uint64_t>::max() / 2) {
      hi = std::numeric_limits<std::uint64_t>::max();
      break;
    }
    hi *= 2;
  }

  while (lo + 1 < hi) {
    const std::uint64_t mid = lo + (hi - lo) / 2;
    if (square_ge(mid, n)) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  return hi;
}

SeparatorState canonicalize_separator(const SeparatorState& state) {
  SeparatorState out;
  out.carry_atoms = state.carry_atoms;
  std::sort(out.carry_atoms.begin(), out.carry_atoms.end(),
            [](const CarryAtom& a, const CarryAtom& b) {
              if (a.id != b.id) {
                return a.id < b.id;
              }
              return a.norm_sq < b.norm_sq;
            });

  std::vector<std::pair<std::vector<AtomId>, bool>> components;
  components.reserve(state.component_partition.size());
  for (std::size_t i = 0; i < state.component_partition.size(); ++i) {
    std::vector<AtomId> component = state.component_partition[i];
    std::sort(component.begin(), component.end());
    component.erase(std::unique(component.begin(), component.end()),
                    component.end());
    std::vector<AtomId> inventory =
        i < state.component_inventory.size() ? state.component_inventory[i]
                                             : component;
    std::sort(inventory.begin(), inventory.end());
    inventory.erase(std::unique(inventory.begin(), inventory.end()),
                    inventory.end());
    const bool source =
        i < state.source_bit_per_component.size()
            ? state.source_bit_per_component[i]
            : false;
    for (const AtomId id : component) {
      if (!std::binary_search(inventory.begin(), inventory.end(), id)) {
        inventory.push_back(id);
      }
    }
    std::sort(inventory.begin(), inventory.end());
    components.push_back({std::move(component), source});
    out.component_inventory.push_back(std::move(inventory));
  }
  std::vector<std::size_t> order(components.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return lexicographic_component_less(components[a].first,
                                        components[b].first);
  });

  std::vector<std::vector<AtomId>> inventories = std::move(out.component_inventory);
  out.component_inventory.clear();
  out.component_partition.reserve(order.size());
  out.source_bit_per_component.reserve(order.size());
  out.component_inventory.reserve(order.size());
  for (const std::size_t index : order) {
    out.component_partition.push_back(std::move(components[index].first));
    out.source_bit_per_component.push_back(components[index].second);
    out.component_inventory.push_back(std::move(inventories[index]));
  }
  return out;
}

ProcessResult process_band(const BandInput& band,
                           const std::optional<SeparatorState>& incoming,
                           const ProcessOptions& options) {
  const std::uint64_t carry_width = ceil_sqrt(band.k_sq);

  if (band.force_overflow) {
    return reject(RejectReason::kOverflow, "band marked overflow", carry_width);
  }
  if (band.outer_radius > 0 &&
      band.outer_radius >
          std::numeric_limits<std::uint64_t>::max() / band.outer_radius) {
    return reject(RejectReason::kMalformed, "outer radius square overflows",
                  carry_width);
  }

  std::vector<BandAtom> all_atoms = band.atoms;
  std::unordered_map<AtomId, std::size_t> index_by_id;
  index_by_id.reserve(all_atoms.size() + (incoming ? incoming->carry_atoms.size()
                                                   : 0));

  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    if (!index_by_id.emplace(all_atoms[i].id, i).second) {
      return reject(RejectReason::kMalformed, "duplicate band atom id",
                    carry_width);
    }
  }

  if (incoming) {
    std::set<AtomId> incoming_atoms;
    for (const CarryAtom& atom : incoming->carry_atoms) {
      if (!incoming_atoms.insert(atom.id).second) {
        return reject(RejectReason::kMalformed, "duplicate incoming carry atom",
                      carry_width);
      }
      const auto existing = index_by_id.find(atom.id);
      if (existing == index_by_id.end()) {
        const std::size_t idx = all_atoms.size();
        all_atoms.push_back({atom.id, atom.norm_sq, false});
        index_by_id.emplace(atom.id, idx);
      } else if (all_atoms[existing->second].norm_sq != atom.norm_sq) {
        return reject(RejectReason::kMalformed,
                      "incoming carry atom norm does not match band atom",
                      carry_width);
      }
    }
    if (incoming->component_partition.size() !=
        incoming->source_bit_per_component.size()) {
      return reject(RejectReason::kMalformed,
                    "incoming source-bit count does not match partition",
                    carry_width);
    }
    if (!incoming->component_inventory.empty() &&
        incoming->component_inventory.size() !=
            incoming->component_partition.size()) {
      return reject(RejectReason::kMalformed,
                    "incoming inventory count does not match partition",
                    carry_width);
    }
    std::set<AtomId> partition_atoms;
    std::set<AtomId> inventory_atoms;
    for (const auto& component : incoming->component_partition) {
      if (component.empty()) {
        return reject(RejectReason::kMalformed,
                      "incoming separator contains empty component",
                      carry_width);
      }
      for (const AtomId id : component) {
        if (incoming_atoms.find(id) == incoming_atoms.end()) {
          return reject(RejectReason::kMalformed,
                        "incoming partition references non-carry atom",
                        carry_width);
        }
        if (!partition_atoms.insert(id).second) {
          return reject(RejectReason::kMalformed,
                        "incoming atom appears in multiple components",
                        carry_width);
        }
      }
    }
    for (std::size_t c = 0; c < incoming->component_inventory.size(); ++c) {
      const auto& inventory = incoming->component_inventory[c];
      if (inventory.empty()) {
        return reject(RejectReason::kMalformed,
                      "incoming separator contains empty inventory",
                      carry_width);
      }
      for (const AtomId id : incoming->component_partition[c]) {
        if (std::find(inventory.begin(), inventory.end(), id) ==
            inventory.end()) {
          return reject(RejectReason::kMalformed,
                        "incoming inventory omits carry atom", carry_width);
        }
      }
      for (const AtomId id : inventory) {
        if (!inventory_atoms.insert(id).second) {
          return reject(RejectReason::kMalformed,
                        "incoming atom appears in multiple inventories",
                        carry_width);
        }
      }
    }
    if (partition_atoms != incoming_atoms) {
      return reject(RejectReason::kMalformed,
                    "incoming partition does not cover all carry atoms",
                    carry_width);
    }
  }

  if (all_atoms.size() > options.max_atoms) {
    return reject(RejectReason::kOverflow, "atom count exceeds source cap",
                  carry_width);
  }

  Dsu dsu(all_atoms.size());
  if (incoming) {
    for (const auto& component : incoming->component_partition) {
      const AtomId first_id = component.front();
      const auto first = index_by_id.find(first_id);
      assert(first != index_by_id.end());
      for (std::size_t i = 1; i < component.size(); ++i) {
        const auto it = index_by_id.find(component[i]);
        assert(it != index_by_id.end());
        dsu.unite(first->second, it->second);
      }
    }
  }

  for (const auto& [a, b] : band.edges) {
    const auto ia = index_by_id.find(a);
    const auto ib = index_by_id.find(b);
    if (ia == index_by_id.end() || ib == index_by_id.end()) {
      return reject(RejectReason::kMalformed, "edge references missing atom",
                    carry_width);
    }
    dsu.unite(ia->second, ib->second);
  }

  std::vector<bool> root_is_source(all_atoms.size(), false);
  bool any_source = false;
  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    if (all_atoms[i].certified_source) {
      root_is_source[dsu.find(i)] = true;
      any_source = true;
    }
  }
  if (incoming) {
    for (std::size_t c = 0; c < incoming->component_partition.size(); ++c) {
      if (!incoming->source_bit_per_component[c]) {
        continue;
      }
      const AtomId id = incoming->component_partition[c].front();
      const auto it = index_by_id.find(id);
      assert(it != index_by_id.end());
      root_is_source[dsu.find(it->second)] = true;
      any_source = true;
    }
  }

  std::map<std::size_t, std::vector<AtomId>> carry_by_root;
  std::map<std::size_t, std::vector<AtomId>> inventory_by_root;
  std::unordered_map<AtomId, std::uint64_t> norm_by_id;
  norm_by_id.reserve(all_atoms.size());

  for (std::size_t i = 0; i < all_atoms.size(); ++i) {
    norm_by_id.emplace(all_atoms[i].id, all_atoms[i].norm_sq);
    const std::size_t root = dsu.find(i);
    inventory_by_root[root].push_back(all_atoms[i].id);
    if (in_final_carry_window(all_atoms[i].norm_sq, band.outer_radius,
                              carry_width)) {
      carry_by_root[root].push_back(all_atoms[i].id);
    }
  }
  if (incoming) {
    for (std::size_t c = 0; c < incoming->component_partition.size(); ++c) {
      const AtomId id = incoming->component_partition[c].front();
      const auto it = index_by_id.find(id);
      assert(it != index_by_id.end());
      std::vector<AtomId>& payload = inventory_by_root[dsu.find(it->second)];
      const std::vector<AtomId>& inventory =
          incoming->component_inventory.empty()
              ? incoming->component_partition[c]
              : incoming->component_inventory[c];
      payload.insert(payload.end(), inventory.begin(), inventory.end());
    }
  }
  for (auto& [root, inventory] : inventory_by_root) {
    std::sort(inventory.begin(), inventory.end());
    inventory.erase(std::unique(inventory.begin(), inventory.end()),
                    inventory.end());
  }

  ProcessResult result;
  result.carry_width = carry_width;
  result.outgoing.component_partition.reserve(carry_by_root.size());
  result.outgoing.source_bit_per_component.reserve(carry_by_root.size());

  for (auto& [root, ids] : carry_by_root) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.empty()) {
      continue;
    }
    result.outgoing.component_partition.push_back(ids);
    result.outgoing.source_bit_per_component.push_back(root_is_source[root]);
    result.outgoing.component_inventory.push_back(inventory_by_root[root]);
    for (const AtomId id : ids) {
      result.outgoing.carry_atoms.push_back({id, norm_by_id.at(id)});
    }
  }

  result.outgoing = canonicalize_separator(result.outgoing);
  if (result.outgoing.carry_atoms.size() > options.max_carry_atoms ||
      result.outgoing.component_partition.size() > options.max_components) {
    return reject(RejectReason::kOverflow,
                  "separator state exceeds source caps", carry_width);
  }

  const bool has_source_carry =
      std::find(result.outgoing.source_bit_per_component.begin(),
                result.outgoing.source_bit_per_component.end(),
                true) != result.outgoing.source_bit_per_component.end();
  result.terminal_source_dead = any_source && !has_source_carry;
  if (result.terminal_source_dead) {
    std::vector<AtomId> source_inventory;
    for (const auto& [root, inventory] : inventory_by_root) {
      if (root_is_source[root]) {
        source_inventory.insert(source_inventory.end(), inventory.begin(),
                                inventory.end());
      }
    }
    std::sort(source_inventory.begin(), source_inventory.end());
    source_inventory.erase(
        std::unique(source_inventory.begin(), source_inventory.end()),
        source_inventory.end());
    result.terminal_source_inventory = std::move(source_inventory);
  }
  return result;
}

ProcessResult process_bands(const std::vector<BandInput>& bands,
                            const std::optional<SeparatorState>& incoming,
                            const ProcessOptions& options) {
  std::optional<SeparatorState> state = incoming;
  ProcessResult last;
  for (const BandInput& band : bands) {
    last = process_band(band, state, options);
    if (!last.accepted()) {
      return last;
    }
    if (last.terminal_source_dead) {
      return last;
    }
    state = last.outgoing;
  }
  return last;
}

std::string reject_reason_name(RejectReason reason) {
  switch (reason) {
    case RejectReason::kNone:
      return "none";
    case RejectReason::kOverflow:
      return "overflow";
    case RejectReason::kMalformed:
      return "malformed";
  }
  return "unknown";
}

}  // namespace lb_source
