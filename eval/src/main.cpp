#include "test_predicate.hpp"

#include <cstring>
#include <iostream>
#include <string>

static const std::string kDefaultJsonPath = "../aig/predicate.json";

static void print_usage(const char *prog) {
  std::cout << "Usage: " << prog << " [options]\n"
            << "\n"
            << "Options:\n"
            << "  --test-predicate [path]   Run predicate self-test\n"
            << "  --help                    Show this message\n";
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
