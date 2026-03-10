#include "parser.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json read_json_file(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("Failed to open JSON file: " + path);
  }
  json j;
  in >> j;
  return j;
}

static void require(bool cond, const std::string &msg) {
  if (!cond)
    throw std::runtime_error("Validation error: " + msg);
}

PredicateDag load_predicate(const std::string &path) {
  json j = read_json_file(path);

  PredicateDag dag;

  dag.export_id = j.at("export_id").get<std::string>();
  dag.root_lit = j.at("root_lit").get<int>();
  dag.constant_false_lit = j.at("constant_false_lit").get<int>();
  dag.constant_true_lit = j.at("constant_true_lit").get<int>();

  dag.input_lits = j.at("inputs").get<std::vector<int>>();

  const auto &annots = j.at("input_annotations");
  for (const auto &a : annots) {
    InputVar iv;
    iv.lit = a.at("lit").get<int>();
    iv.signal = a.at("signal").get<std::string>();
    iv.bit = a.at("bit").get<int>();
    iv.time = a.at("time").get<int>();
    dag.inputs.push_back(std::move(iv));
  }

  const auto &jnodes = j.at("nodes");
  for (const auto &n : jnodes) {
    AndNode nd;
    nd.id = n.at("id").get<int>();
    nd.lhs_lit = n.at("lhs").get<int>();
    nd.rhs_lit = n.at("rhs").get<int>();
    dag.nodes.push_back(nd);
  }

  dag.build_lookups();

  for (int lit : dag.input_lits) {
    require(dag.input_by_lit.count(lit),
            "Input literal " + std::to_string(lit) + " has no annotation");
  }
  for (const auto &iv : dag.inputs) {
    require(dag.input_lit_set.count(iv.lit), "Annotation for literal " +
                                                 std::to_string(iv.lit) +
                                                 " is not in inputs list");
  }

  {
    std::unordered_set<int> seen_ids;
    for (const auto &nd : dag.nodes) {
      require(seen_ids.insert(nd.id).second,
              "Duplicate node id: " + std::to_string(nd.id));
    }
  }

  std::unordered_set<int> node_out_base_lits;
  for (const auto &nd : dag.nodes) {
    node_out_base_lits.insert(2 * nd.id);
  }
  auto is_resolvable = [&](int lit) -> bool {
    if (is_constant_false(lit) || is_constant_true(lit))
      return true;
    int base = positive_base_literal(lit);
    if (dag.input_lit_set.count(base))
      return true;
    if (node_out_base_lits.count(base))
      return true;
    return false;
  };

  require(is_resolvable(dag.root_lit),
          "root_lit " + std::to_string(dag.root_lit) +
              " is not a constant, input, or node output literal");
  for (const auto &nd : dag.nodes) {
    require(is_resolvable(nd.lhs_lit),
            "Node " + std::to_string(nd.id) + " lhs_lit " +
                std::to_string(nd.lhs_lit) + " is not resolvable");
    require(is_resolvable(nd.rhs_lit),
            "Node " + std::to_string(nd.id) + " rhs_lit " +
                std::to_string(nd.rhs_lit) + " is not resolvable");
  }
  return dag;
}
