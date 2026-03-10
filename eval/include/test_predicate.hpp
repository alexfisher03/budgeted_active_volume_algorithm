#pragma once

#include <string>

// Run the full predicate self-test workflow:
//   load → manifest → satisfying eval with debug → failing evals → report.
// Returns 0 on all-pass, 1 on any failure.
int run_predicate_self_test(const std::string &json_path);
