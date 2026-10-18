#include "../include/RAT.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cassert>

int RAT::readRAT(int regNum) const { return specRAT[regNum]; }

void RAT::setSpecRAT(int regNum, int PRF_id) {
  assert(PRF_id !=
         InvalidPhy); // P0-dead invariant: mappings are real registers
  specRAT[regNum] = PRF_id;
}

OperandInfo RAT::readOperand(int regNum) const {
  if (regNum == 0)
    return {true, 0, InvalidPhy};
  int phy = specRAT[regNum];
  return {false, 0, phy};
}

void RAT::tick(const RATInput &input, systemState &CPUstate) {
  static_assert(REGISTER_CAP == 32 && PRF_CAP <= 64);
  const auto &step = input.recovery;
  SRAM<1, 192, 6>::Input port{};
  port.enable = input.squashDetect.needSquash;
  if (step.active) {
    bool open = step.first() || replayOpen;
    uint8_t restored[REGISTER_CAP];
    for (int i = 0; i < REGISTER_CAP; ++i)
      restored[i] = step.first() ? ((archValid & (1u << i)) ? archStorage.readLane(i) : i) : specRAT[i];
    RobTag tag = step.nextTag;
    for (unsigned lane = 0; lane < RECOVERY_WIDTH; ++lane) {
      const unsigned offset = unsigned(step.offset) + lane;
      if (open && offset < ROB_CAP && offset < step.liveCount) {
        const auto &entry = input.replay[lane];
        if (entry.tag == tag) {
          const auto rd = entry.rd;
          const auto phy = entry.newPhy;
          if (rd != 0 && phy != InvalidPhy) restored[rd] = phy;
          if (tag == step.squashTag) open = false;
        } else open = false;
      }
      tag = robNextTag(tag);
    }
    for (int i = 0; i < REGISTER_CAP; ++i)
      CPUstate.RATModule.specRAT[i] = i == 0 ? 0 : restored[i];
    CPUstate.RATModule.replayOpen = open;
  } else if (!input.squashDetect.needSquash && input.issuePacket.valid && input.issuePacket.allocDest) {
    CPUstate.RATModule.specRAT[input.issuePacket.robEntry.dest] = input.issuePacket.phy;
  }
  if (!input.squashDetect.needSquash && !step.active && input.ROBModule.willCommit(input.squashDetect)) {
    auto index = robSlot(input.ROBModule.getHead());
    auto dest = input.ROBModule.getRd(index);
    auto newPhy = input.ROBModule.getNewPhy(index);
    if (dest != 0 && newPhy != InvalidPhy) {
      port.enable = port.writeEnable = true;
      port.writeMask[dest] = true;
      port.writeData[dest] = newPhy;
      CPUstate.RATModule.archValid = archValid | (1u << dest);
    }
  }
  archStorage.tick(port, CPUstate.RATModule.archStorage);
}
