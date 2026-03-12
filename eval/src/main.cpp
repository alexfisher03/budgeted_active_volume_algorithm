#include "budgeted_scheduler.hpp"
#include "portfolio_scheduler.hpp"
#include "store_all_scheduler.hpp"
#include "test_predicate.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

static const std::string kDefaultJsonPath = "../aig/predicate.json";

static void print_usage(const char *prog) {
  std::cout << "Usage: " << prog << " [options]\n"
            << "\n"
            << "Options:\n"
            << "  --test-predicate [path]          Run predicate self-test\n"
            << "  --run-store-all  [path]          Run store-all scheduler\n"
            << "  --run-oldest-live [path] --budget <B>\n"
            << "                                   Single budget, full trace\n"
            << "  --run-oldest-live [path] --budget-list <lo:hi> [--out <file>]\n"
            << "                                   Sweep budgets, metrics\n"
            << "  --run-portfolio [path] --budget <B> [--trace]\n"
            << "                                   Single budget, all members\n"
            << "  --run-portfolio [path] --budget-list <lo:hi> [--out <file>]\n"
            << "                                   Sweep budgets, portfolio-best\n"
            << "  --help                           Show this message\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  try {
    for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "--test-predicate") == 0) {
        std::string path = kDefaultJsonPath;
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          path = argv[++i];
        }
        return run_predicate_self_test(path);
      }

      if (std::strcmp(argv[i], "--run-store-all") == 0) {
        std::string path = kDefaultJsonPath;
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          path = argv[++i];
        }
        return run_store_all_demo(path);
      }

      if (std::strcmp(argv[i], "--run-oldest-live") == 0) {
        std::string path = kDefaultJsonPath;
        std::size_t budget = 0;
        bool have_budget = false;
        std::size_t list_lo = 0, list_hi = 0;
        bool have_list = false;
        std::string out_path;

        while (i + 1 < argc) {
          if (std::strcmp(argv[i + 1], "--budget") == 0) {
            if (i + 2 >= argc) {
              std::cerr << "Error: --budget requires a value\n";
              return 1;
            }
            budget = static_cast<std::size_t>(std::stoull(argv[i + 2]));
            have_budget = true;
            i += 2;
          } else if (std::strcmp(argv[i + 1], "--budget-list") == 0) {
            if (i + 2 >= argc) {
              std::cerr << "Error: --budget-list requires <lo:hi>\n";
              return 1;
            }
            const char *range = argv[i + 2];
            const char *colon = std::strchr(range, ':');
            if (!colon) {
              std::cerr << "Error: --budget-list range must be lo:hi\n";
              return 1;
            }
            list_lo =
                static_cast<std::size_t>(std::strtoull(range, nullptr, 10));
            list_hi =
                static_cast<std::size_t>(std::strtoull(colon + 1, nullptr, 10));
            if (list_lo == 0 || list_hi == 0 || list_lo > list_hi) {
              std::cerr << "Error: invalid range " << range
                        << " (need 1 <= lo <= hi)\n";
              return 1;
            }
            have_list = true;
            i += 2;
          } else if (std::strcmp(argv[i + 1], "--out") == 0) {
            if (i + 2 >= argc) {
              std::cerr << "Error: --out requires a file path\n";
              return 1;
            }
            out_path = argv[i + 2];
            i += 2;
          } else if (argv[i + 1][0] != '-') {
            path = argv[++i];
          } else {
            break;
          }
        }

        if (have_budget && have_list) {
          std::cerr << "Error: use --budget or --budget-list, not both\n";
          return 1;
        }
        if (!have_budget && !have_list) {
          std::cerr << "Error: --run-oldest-live requires "
                       "--budget <B> or --budget-list <lo:hi>\n";
          return 1;
        }

        if (have_list)
          return run_budget_list(path, list_lo, list_hi, out_path);
        return run_oldest_live_demo(path, budget);
      }

      if (std::strcmp(argv[i], "--run-portfolio") == 0) {
        std::string path = kDefaultJsonPath;
        std::size_t budget = 0;
        bool have_budget = false;
        std::size_t list_lo = 0, list_hi = 0;
        bool have_list = false;
        std::string out_path;
        bool trace = false;

        while (i + 1 < argc) {
          if (std::strcmp(argv[i + 1], "--budget") == 0) {
            if (i + 2 >= argc) {
              std::cerr << "Error: --budget requires a value\n";
              return 1;
            }
            budget = static_cast<std::size_t>(std::stoull(argv[i + 2]));
            have_budget = true;
            i += 2;
          } else if (std::strcmp(argv[i + 1], "--budget-list") == 0) {
            if (i + 2 >= argc) {
              std::cerr << "Error: --budget-list requires <lo:hi>\n";
              return 1;
            }
            const char *range = argv[i + 2];
            const char *colon = std::strchr(range, ':');
            if (!colon) {
              std::cerr << "Error: --budget-list range must be lo:hi\n";
              return 1;
            }
            list_lo =
                static_cast<std::size_t>(std::strtoull(range, nullptr, 10));
            list_hi =
                static_cast<std::size_t>(std::strtoull(colon + 1, nullptr, 10));
            if (list_lo == 0 || list_hi == 0 || list_lo > list_hi) {
              std::cerr << "Error: invalid range " << range
                        << " (need 1 <= lo <= hi)\n";
              return 1;
            }
            have_list = true;
            i += 2;
          } else if (std::strcmp(argv[i + 1], "--out") == 0) {
            if (i + 2 >= argc) {
              std::cerr << "Error: --out requires a file path\n";
              return 1;
            }
            out_path = argv[i + 2];
            i += 2;
          } else if (std::strcmp(argv[i + 1], "--trace") == 0) {
            trace = true;
            ++i;
          } else if (argv[i + 1][0] != '-') {
            path = argv[++i];
          } else {
            break;
          }
        }

        if (have_budget && have_list) {
          std::cerr << "Error: use --budget or --budget-list, not both\n";
          return 1;
        }
        if (!have_budget && !have_list) {
          std::cerr << "Error: --run-portfolio requires "
                       "--budget <B> or --budget-list <lo:hi>\n";
          return 1;
        }

        if (have_list)
          return run_portfolio_budget_list(path, list_lo, list_hi, out_path);
        return run_portfolio_demo(path, budget, trace);
      }

      if (std::strcmp(argv[i], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
      }

      std::cerr << "Unknown option: " << argv[i] << "\n";
      print_usage(argv[0]);
      return 1;
    }
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
