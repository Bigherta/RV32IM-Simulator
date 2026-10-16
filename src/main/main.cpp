#include "../include/CPU.hpp"
#include <cstdlib>
int main() {
  legacyHaltEnabled = std::getenv("LEGACY_HALT") != nullptr;
  Memory mem;
  mem.load_ins();
  CPU cpu(mem);
  cpu.run();
}
