#include "independent_moat.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using namespace moat_verify;

namespace {

constexpr int kSourcePropFixtureSchemaVersion = 1;

struct Atom {
  std::string id;
  std::uint64_t r = 0;
};

struct Edge {
  std::string a;
  std::string b;
  std::string band_id;
};

struct Band {
  std::string id;
  std::uint64_t r_inner = 0;
  std::uint64_t r_outer = 0;
};

struct StateClass {
  std::vector<std::string> atoms;
  bool source = false;
  std::vector<std::string> inventory;
};

struct SeparatorState {
  std::uint64_t cut = 0;
  std::vector<StateClass> classes;
};

struct RunResult {
  SeparatorState state;
  bool terminal_source_dead = false;
  std::vector<std::string> terminal_inventory;
};

struct Fixture {
  std::string case_id;
  std::uint32_t k_sq = 0;
  std::uint64_t carry_width = 0;
  std::vector<Atom> atoms;
  std::vector<Edge> edges;
  std::vector<Band> bands;
  std::vector<std::string> source_atoms;
  std::set<std::string> guards;
  std::map<std::string, std::uint64_t> overflow_counters;
  std::vector<std::string> expected_terminal_inventory;
  std::unordered_map<std::string, std::size_t> atom_by_id;
};

enum class HandoffPolicy {
  Exact,
  WeldAllCarryAsSource,
  DropNonSourceCarry,
};

std::string type_name(const nlohmann::json& value) {
  return std::string(value.type_name());
}

const nlohmann::json& require_field(const nlohmann::json& object,
                                    const char* field) {
  const auto it = object.find(field);
  if (it == object.end()) {
    throw std::runtime_error(std::string("missing required field: ") + field);
  }
  return *it;
}

void reject_unknown_fields(const nlohmann::json& object,
                           const std::set<std::string>& allowed,
                           const std::string& context) {
  for (const auto& item : object.items()) {
    if (!allowed.contains(item.key())) {
      throw std::runtime_error(context + " has unknown field: " + item.key());
    }
  }
}

std::uint64_t require_u64(const nlohmann::json& object, const char* field) {
  const nlohmann::json& value = require_field(object, field);
  if (!value.is_number_unsigned()) {
    throw std::runtime_error(std::string("field ") + field +
                             " must be an unsigned integer, got " +
                             type_name(value));
  }
  return value.get<std::uint64_t>();
}

std::uint32_t require_u32(const nlohmann::json& object, const char* field) {
  const std::uint64_t value = require_u64(object, field);
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error(std::string("field ") + field +
                             " exceeds uint32 range");
  }
  return static_cast<std::uint32_t>(value);
}

std::string require_string(const nlohmann::json& object, const char* field) {
  const nlohmann::json& value = require_field(object, field);
  if (!value.is_string()) {
    throw std::runtime_error(std::string("field ") + field +
                             " must be a string, got " + type_name(value));
  }
  return value.get<std::string>();
}

const nlohmann::json& require_array(const nlohmann::json& object,
                                    const char* field) {
  const nlohmann::json& value = require_field(object, field);
  if (!value.is_array()) {
    throw std::runtime_error(std::string("field ") + field +
                             " must be an array, got " + type_name(value));
  }
  return value;
}

bool sane_token(const std::string& value) {
  if (value.empty() || value.size() > 128) return false;
  for (const unsigned char ch : value) {
    if (std::iscntrl(ch) != 0 || std::isspace(ch) != 0) return false;
  }
  return true;
}

std::string require_id(const nlohmann::json& object,
                       const char* field,
                       const std::string& context) {
  const std::string value = require_string(object, field);
  if (!sane_token(value)) {
    throw std::runtime_error(context + " field " + field +
                             " must be nonempty, whitespace-free, and at most "
                             "128 bytes");
  }
  return value;
}

std::uint64_t radius_diff(const Atom& lhs, const Atom& rhs) {
  return lhs.r >= rhs.r ? lhs.r - rhs.r : rhs.r - lhs.r;
}

std::string render_state(const SeparatorState& state) {
  std::ostringstream out;
  out << "cut=" << state.cut;
  for (const StateClass& cls : state.classes) {
    out << "|";
    out << (cls.source ? "S:" : "N:");
    for (std::size_t i = 0; i < cls.atoms.size(); ++i) {
      if (i != 0) out << ",";
      out << cls.atoms[i];
    }
    out << "]{";
    for (std::size_t i = 0; i < cls.inventory.size(); ++i) {
      if (i != 0) out << ",";
      out << cls.inventory[i];
    }
    out << "}";
  }
  return out.str();
}

bool same_state(const SeparatorState& lhs, const SeparatorState& rhs) {
  if (lhs.cut != rhs.cut || lhs.classes.size() != rhs.classes.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.classes.size(); ++i) {
    if (lhs.classes[i].source != rhs.classes[i].source ||
        lhs.classes[i].atoms != rhs.classes[i].atoms ||
        lhs.classes[i].inventory != rhs.classes[i].inventory) {
      return false;
    }
  }
  return true;
}

bool any_source_class(const SeparatorState& state) {
  for (const StateClass& cls : state.classes) {
    if (cls.source) return true;
  }
  return false;
}

std::string guard_list(const Fixture& fixture) {
  std::ostringstream out;
  bool first = true;
  for (const std::string& guard : fixture.guards) {
    if (!first) out << ",";
    first = false;
    out << guard;
  }
  return out.str();
}

void add_unique(std::vector<std::size_t>& values, std::size_t value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

std::size_t atom_index(const Fixture& fixture, const std::string& id) {
  const auto it = fixture.atom_by_id.find(id);
  if (it == fixture.atom_by_id.end()) {
    throw std::runtime_error("unknown atom id: " + id);
  }
  return it->second;
}

std::uint64_t band_outer(const Fixture& fixture, const std::string& id) {
  for (const Band& band : fixture.bands) {
    if (band.id == id) return band.r_outer;
  }
  throw std::runtime_error("unknown band id: " + id);
}

SeparatorState build_separator_state(
    const Fixture& fixture,
    std::uint64_t cut,
    std::uint64_t carry_width,
    const std::vector<std::size_t>& active_atoms,
    const std::unordered_map<std::size_t, std::size_t>& local_by_global,
    Dsu& dsu,
    const std::vector<std::uint8_t>& root_source,
    const std::map<std::size_t, std::set<std::string>>* prior_payload) {
  SeparatorState state;
  state.cut = cut;
  const std::uint64_t carry_lo = cut > carry_width ? cut - carry_width : 0;

  std::map<std::size_t, StateClass> by_root;
  std::map<std::size_t, std::set<std::string>> payload_by_root;
  for (const std::size_t global_idx : active_atoms) {
    const Atom& atom = fixture.atoms[global_idx];
    const std::size_t local_idx = local_by_global.at(global_idx);
    const std::size_t root = dsu.find(local_idx);
    payload_by_root[root].insert(atom.id);
    if (atom.r < carry_lo || atom.r > cut) continue;
    StateClass& cls = by_root[root];
    cls.source = cls.source || root_source[dsu.find(root)] != 0;
    cls.atoms.push_back(atom.id);
  }
  if (prior_payload != nullptr) {
    for (const auto& [root, payload] : *prior_payload) {
      std::set<std::string>& merged = payload_by_root[dsu.find(root)];
      merged.insert(payload.begin(), payload.end());
    }
  }

  for (auto& item : by_root) {
    StateClass& cls = item.second;
    std::sort(cls.atoms.begin(), cls.atoms.end());
    const auto payload_it = payload_by_root.find(dsu.find(item.first));
    if (payload_it != payload_by_root.end()) {
      cls.inventory.assign(payload_it->second.begin(), payload_it->second.end());
    } else {
      cls.inventory = cls.atoms;
    }
    state.classes.push_back(cls);
  }
  std::sort(state.classes.begin(), state.classes.end(),
            [](const StateClass& lhs, const StateClass& rhs) {
              if (lhs.atoms.empty() || rhs.atoms.empty()) {
                return lhs.atoms.size() < rhs.atoms.size();
              }
              return lhs.atoms.front() < rhs.atoms.front();
            });
  return state;
}

std::vector<std::uint8_t> root_sources(Dsu& dsu,
                                       std::size_t n,
                                       const std::vector<std::size_t>& seeds) {
  std::vector<std::uint8_t> source(n, 0);
  for (const std::size_t seed : seeds) {
    source[dsu.find(seed)] = 1;
  }
  return source;
}

SeparatorState run_big_to_cut(const Fixture& fixture,
                              std::uint64_t cut,
                              std::uint64_t carry_width) {
  std::vector<std::size_t> active_atoms;
  std::unordered_map<std::size_t, std::size_t> local_by_global;
  for (std::size_t i = 0; i < fixture.atoms.size(); ++i) {
    if (fixture.atoms[i].r <= cut) {
      local_by_global.emplace(i, active_atoms.size());
      active_atoms.push_back(i);
    }
  }

  Dsu dsu(active_atoms.size());
  for (const Edge& edge : fixture.edges) {
    if (!edge.band_id.empty() && band_outer(fixture, edge.band_id) > cut) {
      continue;
    }
    const std::size_t a = atom_index(fixture, edge.a);
    const std::size_t b = atom_index(fixture, edge.b);
    const auto ia = local_by_global.find(a);
    const auto ib = local_by_global.find(b);
    if (ia == local_by_global.end() || ib == local_by_global.end()) continue;
    dsu.unite(ia->second, ib->second);
  }

  std::vector<std::size_t> source_seeds;
  for (const std::string& source_id : fixture.source_atoms) {
    const std::size_t global_idx = atom_index(fixture, source_id);
    const auto it = local_by_global.find(global_idx);
    if (it != local_by_global.end()) source_seeds.push_back(it->second);
  }
  const std::vector<std::uint8_t> source =
      root_sources(dsu, active_atoms.size(), source_seeds);
  return build_separator_state(fixture, cut, carry_width, active_atoms,
                               local_by_global, dsu, source, nullptr);
}

SeparatorState mutate_for_handoff(SeparatorState state, HandoffPolicy policy) {
  if (policy == HandoffPolicy::Exact) return state;
  if (policy == HandoffPolicy::WeldAllCarryAsSource) {
    for (StateClass& cls : state.classes) cls.source = true;
    return state;
  }
  std::vector<StateClass> kept;
  for (StateClass& cls : state.classes) {
    if (cls.source) kept.push_back(std::move(cls));
  }
  state.classes = std::move(kept);
  return state;
}

RunResult run_composed(const Fixture& fixture,
                       std::uint64_t carry_width,
                       HandoffPolicy policy,
                       bool compare_each_cut) {
  SeparatorState previous;
  bool have_previous = false;
  RunResult result;

  for (std::size_t band_idx = 0; band_idx < fixture.bands.size(); ++band_idx) {
    const Band& band = fixture.bands[band_idx];
    std::vector<std::size_t> active_atoms;
    std::unordered_map<std::size_t, std::size_t> local_by_global;

    auto add_active = [&](std::size_t global_idx) {
      const auto inserted =
          local_by_global.emplace(global_idx, active_atoms.size());
      if (inserted.second) active_atoms.push_back(global_idx);
    };

    if (have_previous) {
      for (const StateClass& cls : previous.classes) {
        for (const std::string& id : cls.atoms) add_active(atom_index(fixture, id));
      }
    }

    for (std::size_t i = 0; i < fixture.atoms.size(); ++i) {
      const Atom& atom = fixture.atoms[i];
      const bool in_first_band =
          band_idx == 0 && atom.r >= band.r_inner && atom.r <= band.r_outer;
      const bool in_later_band =
          band_idx != 0 && atom.r > band.r_inner && atom.r <= band.r_outer;
      if (in_first_band || in_later_band) add_active(i);
    }

    Dsu dsu(active_atoms.size());
    std::vector<std::size_t> source_seeds;

    if (have_previous) {
      for (const StateClass& cls : previous.classes) {
        if (cls.atoms.empty()) continue;
        const std::size_t first = local_by_global.at(atom_index(fixture, cls.atoms.front()));
        for (std::size_t i = 1; i < cls.atoms.size(); ++i) {
          dsu.unite(first, local_by_global.at(atom_index(fixture, cls.atoms[i])));
        }
        if (cls.source) source_seeds.push_back(first);
      }
    } else {
      for (const std::string& source_id : fixture.source_atoms) {
        const std::size_t global_idx = atom_index(fixture, source_id);
        const auto it = local_by_global.find(global_idx);
        if (it == local_by_global.end()) {
          throw std::runtime_error("source atom is not active in first band: " +
                                   source_id);
        }
        source_seeds.push_back(it->second);
      }
    }

    for (const Edge& edge : fixture.edges) {
      if (!edge.band_id.empty() && edge.band_id != band.id) continue;
      const std::size_t a = atom_index(fixture, edge.a);
      const std::size_t b = atom_index(fixture, edge.b);
      const auto ia = local_by_global.find(a);
      const auto ib = local_by_global.find(b);
      if (ia == local_by_global.end() || ib == local_by_global.end()) continue;
      dsu.unite(ia->second, ib->second);
    }

    const std::vector<std::uint8_t> source =
        root_sources(dsu, active_atoms.size(), source_seeds);
    std::map<std::size_t, std::set<std::string>> prior_payload_by_root;
    if (have_previous) {
      for (const StateClass& cls : previous.classes) {
        if (cls.atoms.empty()) continue;
        const std::size_t root =
            dsu.find(local_by_global.at(atom_index(fixture, cls.atoms.front())));
        const std::vector<std::string>& inventory =
            cls.inventory.empty() ? cls.atoms : cls.inventory;
        prior_payload_by_root[root].insert(inventory.begin(), inventory.end());
      }
    }
    result.state = build_separator_state(
        fixture, band.r_outer, carry_width, active_atoms, local_by_global, dsu,
        source, &prior_payload_by_root);

    std::map<std::size_t, std::set<std::string>> payload_by_root =
        prior_payload_by_root;
    for (const std::size_t global_idx : active_atoms) {
      const std::size_t root = dsu.find(local_by_global.at(global_idx));
      payload_by_root[root].insert(fixture.atoms[global_idx].id);
    }
    std::set<std::string> source_payload;
    for (const auto& [root, payload] : payload_by_root) {
      if (source[dsu.find(root)] != 0) {
        source_payload.insert(payload.begin(), payload.end());
      }
    }
    if (compare_each_cut) {
      const SeparatorState big =
          run_big_to_cut(fixture, band.r_outer, carry_width);
      if (!same_state(result.state, big)) {
        throw std::runtime_error(
            "composed separator mismatch at cut " +
            std::to_string(band.r_outer) + ": composed=" +
            render_state(result.state) + " big=" + render_state(big));
      }
    }

    if (!source_payload.empty() && !any_source_class(result.state)) {
      result.terminal_source_dead = true;
      result.terminal_inventory.assign(source_payload.begin(),
                                       source_payload.end());
      return result;
    }

    previous = result.state;
    if (band_idx + 1 < fixture.bands.size()) {
      previous = mutate_for_handoff(std::move(previous), policy);
    }
    have_previous = true;
  }
  return result;
}

void validate_guards(const Fixture& fixture) {
  constexpr std::array<const char*, 5> allowed = {
      "composed_equals_big",
      "false_weld_guard",
      "drop_non_source_carry_guard",
      "terminal_death_guard",
      "k32_carry_width_minimum"};
  for (const std::string& guard : fixture.guards) {
    if (std::find(allowed.begin(), allowed.end(), guard) == allowed.end()) {
      throw std::runtime_error("unknown guard: " + guard);
    }
  }
}

Fixture parse_fixture(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) throw std::runtime_error("could not open fixture: " + path);
  nlohmann::json root;
  in >> root;
  if (!root.is_object()) {
    throw std::runtime_error("fixture root must be a JSON object");
  }
  reject_unknown_fields(root,
                        {"schema_version", "case_id", "k_sq", "carry_width",
                         "source_atoms", "atoms", "edges", "bands", "guards",
                         "overflow_counters",
                         "expected_terminal_inventory", "notes"},
                        "fixture");

  Fixture fixture;
  const std::uint32_t schema_version = require_u32(root, "schema_version");
  if (schema_version != kSourcePropFixtureSchemaVersion) {
    throw std::runtime_error("schema_version must be 1");
  }
  fixture.case_id = require_id(root, "case_id", "fixture");
  fixture.k_sq = require_u32(root, "k_sq");
  fixture.carry_width = require_u64(root, "carry_width");

  for (const nlohmann::json& item : require_array(root, "source_atoms")) {
    if (!item.is_string()) {
      throw std::runtime_error("source_atoms entries must be strings");
    }
    const std::string id = item.get<std::string>();
    if (!sane_token(id)) throw std::runtime_error("invalid source atom id: " + id);
    fixture.source_atoms.push_back(id);
  }
  if (fixture.source_atoms.empty()) {
    throw std::runtime_error("source_atoms must not be empty");
  }

  for (const nlohmann::json& item : require_array(root, "atoms")) {
    if (!item.is_object()) throw std::runtime_error("atom entries must be objects");
    reject_unknown_fields(item, {"id", "r"}, "atom");
    Atom atom{require_id(item, "id", "atom"), require_u64(item, "r")};
    if (fixture.atom_by_id.contains(atom.id)) {
      throw std::runtime_error("duplicate atom id: " + atom.id);
    }
    fixture.atom_by_id.emplace(atom.id, fixture.atoms.size());
    fixture.atoms.push_back(std::move(atom));
  }
  if (fixture.atoms.empty()) throw std::runtime_error("atoms must not be empty");

  for (const nlohmann::json& item : require_array(root, "edges")) {
    if (!item.is_object()) throw std::runtime_error("edge entries must be objects");
    reject_unknown_fields(item, {"a", "b", "band"}, "edge");
    Edge edge{require_id(item, "a", "edge"), require_id(item, "b", "edge")};
    if (item.contains("band")) {
      edge.band_id = require_id(item, "band", "edge");
    }
    fixture.edges.push_back(std::move(edge));
  }

  for (const nlohmann::json& item : require_array(root, "bands")) {
    if (!item.is_object()) throw std::runtime_error("band entries must be objects");
    reject_unknown_fields(item, {"id", "r_inner", "r_outer"}, "band");
    fixture.bands.push_back(Band{require_id(item, "id", "band"),
                                 require_u64(item, "r_inner"),
                                 require_u64(item, "r_outer")});
  }
  if (fixture.bands.empty()) throw std::runtime_error("bands must not be empty");

  for (const nlohmann::json& item : require_array(root, "guards")) {
    if (!item.is_string()) throw std::runtime_error("guards entries must be strings");
    fixture.guards.insert(item.get<std::string>());
  }
  if (fixture.guards.empty()) throw std::runtime_error("guards must not be empty");

  if (root.contains("overflow_counters")) {
    const nlohmann::json& counters = root.at("overflow_counters");
    if (!counters.is_object()) {
      throw std::runtime_error("overflow_counters must be an object");
    }
    for (const auto& item : counters.items()) {
      if (!sane_token(item.key())) {
        throw std::runtime_error("invalid overflow counter name: " + item.key());
      }
      if (!item.value().is_number_unsigned()) {
        throw std::runtime_error("overflow counter must be unsigned: " +
                                 item.key());
      }
      fixture.overflow_counters.emplace(item.key(),
                                        item.value().get<std::uint64_t>());
    }
  }
  if (root.contains("expected_terminal_inventory")) {
    const nlohmann::json& inventory = root.at("expected_terminal_inventory");
    if (!inventory.is_array()) {
      throw std::runtime_error("expected_terminal_inventory must be an array");
    }
    for (const nlohmann::json& item : inventory) {
      if (!item.is_string()) {
        throw std::runtime_error(
            "expected_terminal_inventory entries must be strings");
      }
      const std::string id = item.get<std::string>();
      if (!sane_token(id)) {
        throw std::runtime_error("invalid expected terminal atom id: " + id);
      }
      fixture.expected_terminal_inventory.push_back(id);
    }
    std::sort(fixture.expected_terminal_inventory.begin(),
              fixture.expected_terminal_inventory.end());
    const auto last = std::unique(fixture.expected_terminal_inventory.begin(),
                                  fixture.expected_terminal_inventory.end());
    if (last != fixture.expected_terminal_inventory.end()) {
      throw std::runtime_error(
          "expected_terminal_inventory contains duplicates");
    }
  }
  return fixture;
}

void validate_fixture(const Fixture& fixture) {
  if (fixture.k_sq == 0) throw std::runtime_error("k_sq must be positive");
  validate_guards(fixture);

  for (const auto& item : fixture.overflow_counters) {
    if (item.second != 0) {
      throw std::runtime_error("overflow counter is nonzero for source claim: " +
                               item.first);
    }
  }

  const std::uint64_t min_carry_width = ceil_isqrt_u64(fixture.k_sq);
  if (fixture.carry_width < min_carry_width) {
    throw std::runtime_error("carry_width must be at least ceil_sqrt(k_sq)=" +
                             std::to_string(min_carry_width));
  }

  if (fixture.guards.contains("k32_carry_width_minimum")) {
    if (fixture.k_sq != 32 || min_carry_width != 6 ||
        fixture.carry_width != 6) {
      throw std::runtime_error(
          "k32_carry_width_minimum requires k_sq=32 and carry_width=6");
    }
  }

  std::unordered_set<std::string> seen_sources;
  for (const std::string& source_id : fixture.source_atoms) {
    atom_index(fixture, source_id);
    if (!seen_sources.insert(source_id).second) {
      throw std::runtime_error("duplicate source atom id: " + source_id);
    }
  }
  for (const std::string& id : fixture.expected_terminal_inventory) {
    atom_index(fixture, id);
  }

  std::uint64_t previous_outer = 0;
  std::set<std::string> band_ids;
  for (std::size_t i = 0; i < fixture.bands.size(); ++i) {
    const Band& band = fixture.bands[i];
    if (!band_ids.insert(band.id).second) {
      throw std::runtime_error("duplicate band id: " + band.id);
    }
    if (band.r_outer <= band.r_inner) {
      throw std::runtime_error("band r_outer must be greater than r_inner: " +
                               band.id);
    }
    if (i != 0 && band.r_inner != previous_outer) {
      throw std::runtime_error("bands must be contiguous at band: " + band.id);
    }
    previous_outer = band.r_outer;
  }

  const std::uint64_t final_cut = fixture.bands.back().r_outer;
  for (const Atom& atom : fixture.atoms) {
    if (atom.r > final_cut) {
      throw std::runtime_error("atom lies beyond final band cut: " + atom.id);
    }
  }

  std::set<std::pair<std::string, std::string>> seen_edges;
  for (const Edge& edge : fixture.edges) {
    if (edge.a == edge.b) throw std::runtime_error("self edge: " + edge.a);
    if (!edge.band_id.empty() && !band_ids.contains(edge.band_id)) {
      throw std::runtime_error("edge references unknown band: " + edge.band_id);
    }
    const std::size_t a = atom_index(fixture, edge.a);
    const std::size_t b = atom_index(fixture, edge.b);
    const auto key = std::minmax(edge.a, edge.b);
    if (!seen_edges.insert(key).second) {
      throw std::runtime_error("duplicate edge: " + edge.a + "-" + edge.b);
    }
    if (radius_diff(fixture.atoms[a], fixture.atoms[b]) > min_carry_width) {
      throw std::runtime_error("edge exceeds ceil_sqrt(k_sq) radial bound: " +
                               edge.a + "-" + edge.b);
    }
  }
}

void run_fixture(const Fixture& fixture) {
  validate_fixture(fixture);

  const bool compare_each_cut = fixture.guards.contains("composed_equals_big");
  const RunResult exact =
      run_composed(fixture, fixture.carry_width, HandoffPolicy::Exact,
                   compare_each_cut);
  const SeparatorState big = run_big_to_cut(
      fixture, fixture.bands.back().r_outer, fixture.carry_width);
  if (compare_each_cut && !same_state(exact.state, big)) {
    throw std::runtime_error("final composed state differs from big state: "
                             "composed=" +
                             render_state(exact.state) + " big=" +
                             render_state(big));
  }

  if (fixture.guards.contains("false_weld_guard")) {
    const RunResult welded =
        run_composed(fixture, fixture.carry_width,
                     HandoffPolicy::WeldAllCarryAsSource, false);
    if (same_state(welded.state, big)) {
      throw std::runtime_error(
          "false_weld_guard did not distinguish all-carry source wiring");
    }
  }

  if (fixture.guards.contains("drop_non_source_carry_guard")) {
    const RunResult dropped =
        run_composed(fixture, fixture.carry_width,
                     HandoffPolicy::DropNonSourceCarry, false);
    if (same_state(dropped.state, big)) {
      throw std::runtime_error(
          "drop_non_source_carry_guard did not distinguish source-only carry");
    }
  }

  if (fixture.guards.contains("terminal_death_guard")) {
    if (!exact.terminal_source_dead) {
      throw std::runtime_error("terminal_death_guard did not terminal");
    }
    if (exact.terminal_inventory != fixture.expected_terminal_inventory) {
      std::ostringstream msg;
      msg << "terminal inventory mismatch: actual=";
      for (const std::string& id : exact.terminal_inventory) msg << id << ",";
      msg << " expected=";
      for (const std::string& id : fixture.expected_terminal_inventory) {
        msg << id << ",";
      }
      throw std::runtime_error(msg.str());
    }
    if (any_source_class(big)) {
      throw std::runtime_error(
          "terminal_death_guard requires no source class in final carry");
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: source_prop_oracle FIXTURE.json\n";
    return 2;
  }
  try {
    const Fixture fixture = parse_fixture(argv[1]);
    run_fixture(fixture);
    std::cout << "source propagation oracle PASS: case=" << fixture.case_id
              << " guards=" << guard_list(fixture) << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
  }
}
