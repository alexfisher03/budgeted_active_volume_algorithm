#include "budgeted_scheduler.hpp"
#include "store_all_scheduler.hpp"
#include "test_predicate.hpp"

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
            << "                                   Run oldest-live budgeted "
               "scheduler\n"
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

        while (i + 1 < argc) {
          if (std::strcmp(argv[i + 1], "--budget") == 0) {
            if (i + 2 >= argc) {
              std::cerr << "Error: --budget requires a value\n";
              return 1;
            }
            budget = static_cast<std::size_t>(std::stoull(argv[i + 2]));
            have_budget = true;
            i += 2;
          } else if (argv[i + 1][0] != '-') {
            path = argv[++i];
          } else {
            break;
          }
        }

        if (!have_budget) {
          std::cerr << "Error: --run-oldest-live requires --budget <B>\n";
          return 1;
        }
        return run_oldest_live_demo(path, budget);
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
