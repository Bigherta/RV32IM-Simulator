#include "../include/FetchUnit.hpp"
#include "../include/CPU.hpp"

void FetchUnit::clear() {
  programCounter = 0;
  haltFetched = false;
}

void FetchUnit::setPC(uint32_t pc) { programCounter = pc; }

void FetchUnit::setHaltFetched(bool v) { haltFetched = v; }

void FetchUnit::tick(const FetchUnitInput &input, systemState &CPUstate) {
  // stage 3 flush first: on squash restore the PC and clear the halt flag
  if (input.squashDetect.needSquash) {
    CPUstate.FetchUnitModule.programCounter = input.squashDetect.SquashPC;
    CPUstate.FetchUnitModule.haltFetched = false;
    return;
  }
  // stage 2 halt signal: latch when the ICache head holds the halt instruction
  if (input.haltSignal) {
    CPUstate.FetchUnitModule.haltFetched = true;
  }
  // stage 1 normal advance: advance the PC when fetchDecision is valid
  if (input.fetchDecision.valid) {
    CPUstate.FetchUnitModule.programCounter = static_cast<uint32_t>(input.fetchDecision.predictedPC);
  }
}
