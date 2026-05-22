#include "lb_source/source_propagation.h"

#include <iostream>
#include <optional>
#include <vector>

int main() {
  using lb_source::BandAtom;
  using lb_source::BandInput;

  const std::vector<BandInput> bands = {
      BandInput{
          .k_sq = 36,
          .outer_radius = 10,
          .atoms = std::vector<BandAtom>{{1, 25, true}, {2, 100, false}},
          .edges = {{1, 2}},
      },
      BandInput{
          .k_sq = 36,
          .outer_radius = 16,
          .atoms = std::vector<BandAtom>{{3, 196, false}},
          .edges = {{2, 3}},
      },
  };

  const lb_source::ProcessResult result = lb_source::process_bands(bands);
  if (!result.accepted()) {
    std::cerr << "source propagation rejected: "
              << lb_source::reject_reason_name(result.reject) << ": "
              << result.diagnostic << "\n";
    return 1;
  }

  std::cout << "carry_width=" << result.carry_width
            << " carry_atoms=" << result.outgoing.carry_atoms.size()
            << " components=" << result.outgoing.component_partition.size()
            << " terminal="
            << (result.terminal_source_dead ? "true" : "false") << "\n";
  return 0;
}
