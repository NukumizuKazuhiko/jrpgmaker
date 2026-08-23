#include <iostream>

#include "jrpgmaker/core/version.hpp"

auto main() -> int {
  std::cout << "jrpgmaker " << jrpgmaker::core::version() << '\n';
  return 0;
}
