#pragma once

#include <cstddef>
#include <string>

// run diagnostic comparison of all eviction policies at a given budget
// prints: DagAnalysis tables, per-policy eviction decisions with full
// candidate scoring, action traces, and a structural analysis conclusion
int run_diagnose_portfolio(const std::string &json_path, std::size_t budget);
