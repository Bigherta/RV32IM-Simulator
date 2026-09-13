// Order-independence check for the fully module-ized CPU: the same program
// is executed N times, each run drawing a random module work order per cycle
// (dark::CPU::run_once_shuffle), and one result line is printed per run.
// The test passes iff all N lines are identical -- Wire reads are
// structurally tied to the committed _M_old view, so the work order must
// not matter; this binary turns that structural claim into runtime evidence.
#include "../src/include/CPU.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc, char *argv[]) {
  const int iterations = argc > 1 ? std::atoi(argv[1]) : 20;
  Memory base;
  base.load_ins();
  for (int i = 0; i < iterations; ++i) {
    CPU cpu(base); // fresh machine state per run
    cpu.run(/*shuffle=*/true);
  }
  return 0;
}
