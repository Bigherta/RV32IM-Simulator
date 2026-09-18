#include "../include/RAT.hpp"
#include "../include/CPU.hpp"
#include <cassert>

int RAT::readRAT_PRF(int regNum) const { return RAT_PRF[regNum]; }

void RAT::setRAT_PRF(int regNum, int PRF_id) {
  assert(PRF_id != InvalidPhy); // P0-dead invariant: mappings are real registers
  RAT_PRF[regNum] = PRF_id;
}

OperandInfo RAT::readOperand(int regNum) const {
  if (regNum == 0)
    return {true, 0, InvalidPhy};
  int phy = RAT_PRF[regNum];
  return {false, 0, phy};
}

void RAT::restoreRAT_PRF(const RATSnapshot &snapshot) {
  memcpy(RAT_PRF, snapshot.RAT_snapshot, sizeof(RAT_PRF));
}

void RAT::tick(const RATInput &input, systemState &CPUstate) {
  if (input.squashDetect.needSquash) {
    CPUstate.RATModule.restoreRAT_PRF(
        this->ratCkpt[input.squashDetect.CkptId]);
  }
  if (input.issuePacket.valid) {
    const auto &ckptId = input.issuePacket.robEntry.ckptId;
    memcpy(CPUstate.RATModule.ratCkpt[ckptId].RAT_snapshot, this->RAT_PRF,
           sizeof(RAT_PRF));
    if (input.issuePacket.allocDest) {
      CPUstate.RATModule.ratCkpt[ckptId]
          .RAT_snapshot[input.issuePacket.robEntry.dest] =
          input.issuePacket.phy;
      CPUstate.RATModule.setRAT_PRF(input.issuePacket.robEntry.dest,
                                    input.issuePacket.phy);
    }
  }
}
