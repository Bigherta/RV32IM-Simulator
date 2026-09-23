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
void RAT::setArchRAT(int regNum, int PRF_id) {
  assert(PRF_id !=
         InvalidPhy); // P0-dead invariant: mappings are real registers
  archRAT[regNum] = PRF_id;
}

OperandInfo RAT::readOperand(int regNum) const {
  if (regNum == 0)
    return {true, 0, InvalidPhy};
  int phy = specRAT[regNum];
  return {false, 0, phy};
}

void RAT::restoreRAT() { memcpy(specRAT, archRAT, sizeof(specRAT)); }

void RAT::tick(const RATInput &input, systemState &CPUstate) {
  // Squash keeps the offending control instruction in the ROB, so replay its
  // rename as well after restoring the committed architectural baseline.
  if (input.squashDetect.needSquash) {
    const RobTag squashTag = input.squashDetect.SquashTag;
#ifndef NDEBUG
    RobTag cursor = input.ROBModule.getHead();
    bool windowOpen = !input.ROBModule.isEmpty();
    bool squashInWindow = false;
    for (int k = 0; k < ROB_CAP; ++k) {
      if (windowOpen) {
        if (cursor == squashTag)
          squashInWindow = true;
        cursor = robNextTag(cursor);
        if (cursor == input.ROBModule.getNextTag())
          windowOpen = false;
      }
    }
    const bool validSquash =
        squashInWindow && input.ROBModule.matchesTag(squashTag);
    assert(validSquash);
#endif

    CPUstate.RATModule.restoreRAT();
    RobTag replayCursor = input.ROBModule.getHead();
    bool restoreDone = false;
    for (int k = 0; k < ROB_CAP; ++k) {
      if (restoreDone)
        continue;
      const auto index = robSlot(replayCursor);

      const auto dest = input.ROBModule.getRd(index);
      const auto newPhy = input.ROBModule.getNewPhy(index);
      if (dest != 0 && newPhy != InvalidPhy) {
        CPUstate.RATModule.setSpecRAT(dest, newPhy);
      }

      if (replayCursor == squashTag) {
        restoreDone = true;
      } else {
        replayCursor = robNextTag(replayCursor);
      }
    }
  } else {
    // 2. write speculative RAT with issue write intention
    if (input.issuePacket.valid) {
      if (input.issuePacket.allocDest) {
        CPUstate.RATModule.setSpecRAT(input.issuePacket.robEntry.dest,
                                      input.issuePacket.phy);
      }
    }
  }
  // 3. write architecture RAT with ROB commit entry
  if (input.ROBModule.willCommit(input.squashDetect)) {
    auto index = robSlot(input.ROBModule.getHead());
    auto dest = input.ROBModule.getRd(index);
    auto newPhy = input.ROBModule.getNewPhy(index);
    if (dest != 0 && newPhy != InvalidPhy)
      CPUstate.RATModule.setArchRAT(dest, newPhy);
  }
}
