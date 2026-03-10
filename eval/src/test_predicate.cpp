#include "test_predicate.hpp"
#include "evaluator.hpp"
#include "parser.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// ── assignment builders ─────────────────────────────────────

// rst_n = 111
// in_bits = 1010101
static SemanticAssignment make_satisfying_assignment() {
  SemanticAssignment sem;
  sem[{"rst_n", 0, 2}] = true;    // rst_n@2
  sem[{"rst_n", 0, 1}] = true;    // rst_n@1
  sem[{"rst_n", 0, 0}] = true;    // rst_n@0
  sem[{"in_bits", 7, 0}] = true;  // in_bits[7]@0
  sem[{"in_bits", 6, 0}] = false; // in_bits[6]@0
  sem[{"in_bits", 5, 0}] = true;  // in_bits[5]@0
  sem[{"in_bits", 4, 0}] = false; // in_bits[4]@0
  sem[{"in_bits", 3, 0}] = true;  // in_bits[3]@0
  sem[{"in_bits", 2, 0}] = false; // in_bits[2]@0
  sem[{"in_bits", 1, 0}] = true;  // in_bits[1]@0
  return sem;
}

static SemanticAssignment make_unsatisfying_assignment() {
  SemanticAssignment sem;
  sem[{"rst_n", 0, 2}] = true;    // rst_n@2
  sem[{"rst_n", 0, 1}] = true;    // rst_n@1
  sem[{"rst_n", 0, 0}] = false;   // rst_n@0
  sem[{"in_bits", 7, 0}] = false; // in_bits[7]@0
  sem[{"in_bits", 6, 0}] = false; // in_bits[6]@0
  sem[{"in_bits", 5, 0}] = true;  // in_bits[5]@0
  sem[{"in_bits", 4, 0}] = false; // in_bits[4]@0
  sem[{"in_bits", 3, 0}] = true;  // in_bits[3]@0
  sem[{"in_bits", 2, 0}] = false; // in_bits[2]@0
  sem[{"in_bits", 1, 0}] = true;  // in_bits[1]@0
  return sem;
}

static SemanticAssignment make_fail_mask_bit_assignment() {
  auto sem = make_satisfying_assignment();
  sem[{"in_bits", 6, 0}] = true; // flip in_bits[6]@0
  return sem;
}

static SemanticAssignment make_fail_reset_assignment() {
  auto sem = make_satisfying_assignment();
  sem[{"rst_n", 0, 0}] = false; // deassert rst_n@0
  return sem;
}

// ── debug / printing helpers ────────────────────────────────

static void print_predicate_manifest(const PredicateDag &dag) {
  std::cout << "  export_id : " << dag.export_id << "\n";
  std::cout << "  root_lit  : " << dag.root_lit << "\n";
  std::cout << "  inputs    : " << dag.inputs.size() << "\n";
  std::cout << "  nodes     : " << dag.nodes.size() << "\n";
}

static void print_assignment(const PredicateDag &dag,
                             const std::unordered_map<int, bool> &assignment) {
  std::vector<int> sorted_lits;
  for (const auto &[lit, _] : assignment) {
    sorted_lits.push_back(lit);
  }
  std::sort(sorted_lits.begin(), sorted_lits.end());

  for (int lit : sorted_lits) {
    bool val = assignment.at(lit);
    auto it = dag.input_by_lit.find(lit);
    if (it != dag.input_by_lit.end()) {
      const InputVar *iv = it->second;
      std::cout << "  lit " << lit << "  " << iv->signal;
      if (iv->signal == "in_bits") {
        std::cout << "[" << iv->bit << "]";
      }
      std::cout << "@" << iv->time << " = " << val << "\n";
    } else {
      std::cout << "  lit " << lit << " = " << val << "\n";
    }
  }
}

static void print_node_valuation(const PredicateDag &dag,
                                 const EvalResult &result) {
  std::vector<int> nids;
  for (const auto &[nid, _] : result.node_values) {
    nids.push_back(nid);
  }
  std::sort(nids.begin(), nids.end());

  for (int nid : nids) {
    auto nit = dag.node_by_id.find(nid);
    if (nit != dag.node_by_id.end()) {
      const AndNode *n = nit->second;
      std::cout << "  node " << nid << " (lit " << 2 * nid << ")"
                << "  lhs=" << n->lhs_lit << " rhs=" << n->rhs_lit << "  => "
                << result.node_values.at(nid) << "\n";
    } else {
      std::cout << "  node " << nid << " => " << result.node_values.at(nid)
                << "\n";
    }
  }
}

static void print_test_result(const char *test_name, bool expected_root,
                             bool actual_root) {
  bool pass = (actual_root == expected_root);
  std::cout << "  " << (pass ? "PASS" : "FAIL") << " " << test_name
            << " (root = " << actual_root << ")\n";
}

// ── individual test cases ───────────────────────────────────

static bool test_known_satisfying_assignment(const PredicateDag &dag) {
  std::cout << "\n[TEST] test_known_satisfying_assignment\n";

  auto assignment = build_assignment(dag, make_satisfying_assignment());
  auto result = evaluate(dag, assignment);

  std::cout << "\n-- Manifest --\n";
  print_predicate_manifest(dag);
  std::cout << "\n-- Input assignment --\n";
  print_assignment(dag, assignment);
  std::cout << "\n-- Node values (ascending id) --\n";
  print_node_valuation(dag, result);
  std::cout << "\n-- Root --\n";
  std::cout << "  root_lit " << dag.root_lit << " => " << result.root_value
            << "\n";

  print_test_result("test_known_satisfying_assignment", true,
                    result.root_value);
  return result.root_value == true;
}

static bool test_fail_gate_bit(const PredicateDag &dag) {
  std::cout << "\n[TEST] test_fail_gate_bit\n";

  auto assignment = build_assignment(dag, make_unsatisfying_assignment());
  auto result = evaluate(dag, assignment);

  print_test_result("test_fail_gate_bit", false, result.root_value);
  return result.root_value == false;
}

static bool test_fail_mask_bit(const PredicateDag &dag) {
  std::cout << "\n[TEST] test_fail_mask_bit\n";

  auto assignment = build_assignment(dag, make_fail_mask_bit_assignment());
  auto result = evaluate(dag, assignment);

  print_test_result("test_fail_mask_bit", false, result.root_value);
  return result.root_value == false;
}

static bool test_fail_reset(const PredicateDag &dag) {
  std::cout << "\n[TEST] test_fail_reset\n";

  auto assignment = build_assignment(dag, make_fail_reset_assignment());
  auto result = evaluate(dag, assignment);

  print_test_result("test_fail_reset", false, result.root_value);
  return result.root_value == false;
}

// ── public entry point ──────────────────────────────────────

int run_predicate_self_test(const std::string &json_path) {
  std::cout << "Loading predicate from: " << json_path << "\n";
  PredicateDag dag = load_predicate(json_path);
  std::cout << "Loaded " << dag.export_id << " (" << dag.inputs.size()
            << " inputs, " << dag.nodes.size()
            << " nodes, root_lit=" << dag.root_lit << ")\n";

  int passed = 0;
  int total = 4;

  if (test_known_satisfying_assignment(dag))
    ++passed;
  if (test_fail_gate_bit(dag))
    ++passed;
  if (test_fail_mask_bit(dag))
    ++passed;
  if (test_fail_reset(dag))
    ++passed;

  std::cout << "\n============================\n";
  std::cout << "Results: " << passed << "/" << total << " tests passed\n";
  std::cout << "============================\n";

  return (passed == total) ? 0 : 1;
}
