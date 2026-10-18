#include "../include/PRF.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cassert>
#include <cstring>
#include <stdexcept>

PRF::PRF() {
  for (int i = 0; i < PRF_CAP; ++i) {
    PhysicalRegs[i].value = 0;
    PhysicalRegs[i].ready = false;
  }
  // P1-P31 are bound to x1-x31 at reset (RAT[x] = Px, committed init value 0).
  // x0 is never renamed (all issue paths skip rd==0): RAT[0] stays
  // InvalidPhy(=0) and P0 is permanently reserved -- it never enters the free
  // list, which makes InvalidPhy=0 a valid "no register" sentinel everywhere.
  for (int i = 0; i < REGISTER_CAP; ++i)
    PhysicalRegs[i].ready = true;
  // P32..P(PRF_CAP-1) enter the free list; empty tail slots stay InvalidPhy.
  memset(freeList, 0, sizeof(freeList));
  for (int i = REGISTER_CAP; i < PRF_CAP; ++i) {
    freeList[prfSlot(tailSeq)] = i;
    tailSeq = prfSeqNext(tailSeq);
  }
}
uint8_t PRF::pop(systemState &CPUstate) const {
  if (isFreeListEmpty())
    throw std::runtime_error("PRF free list underflow!");
  uint8_t phy = freeList[prfSlot(headSeq)];
  assert(phy !=
         InvalidPhy); // P0-dead invariant: popped tags are real registers
  CPUstate.PRFModule.headSeq = prfSeqNext(headSeq);
  CPUstate.PRFModule.PhysicalRegs[phy].ready = false;
  return phy;
}

void PRF::push(int index, PrfSeq &writeTail, systemState &CPUstate) const {
  assert(index != InvalidPhy); // P0-dead invariant: only real tags are recycled
  assert(prfSeqDistance(headSeq, writeTail) <
         PRF_CAP); // free list never overflows
  CPUstate.PRFModule.freeList[prfSlot(writeTail)] = index;
  writeTail = prfSeqNext(writeTail);
  CPUstate.PRFModule.tailSeq = writeTail;
}

bool PRF::isFreeListEmpty() const { return headSeq == tailSeq; }

PrfSeq PRF::getHeadSeq() const { return headSeq; }

bool PRF::isReady(int index) const { return PhysicalRegs[index].ready; }
int32_t PRF::getValue(int index) const { return PhysicalRegs[index].value; }
void PRF::write(int index, int32_t value, systemState &CPUstate) const {
  CPUstate.PRFModule.PhysicalRegs[index].ready = true;
  CPUstate.PRFModule.PhysicalRegs[index].value = value;
}

void PRF::tick(const PRFInput &input, systemState &CPUstate) {
  PrfSeq writeTail = tailSeq; // Multiple recycle writes share one local cursor.
  // 1. cdb of alu wake up prf
  if (input.cdbOfALU.valid &&
      input.ROBModule.matchesTag(input.cdbOfALU.robTag)) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfALU.robTag, input.squashDetect.SquashTag)) {
      auto robIdx = robSlot(input.cdbOfALU.robTag);
      auto isControl = input.cdbOfALU.isControl;
      if (!isControl) {
        auto value = input.cdbOfALU.value;
        int newPhy = input.ROBModule.getNewPhy(robIdx);
        if (newPhy != InvalidPhy) {
          write(newPhy, value, CPUstate);
        }
      }
    }
  }
  // 2. Shared MUL/DIV/LQ result: exactly one physical write port.
  if (input.cdbOfShared.valid &&
      input.ROBModule.matchesTag(input.cdbOfShared.robTag)) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfShared.robTag, input.squashDetect.SquashTag)) {
      auto robIdx = robSlot(input.cdbOfShared.robTag);
      auto value = input.cdbOfShared.value;
      int newPhy = input.ROBModule.getNewPhy(robIdx);
      if (newPhy != InvalidPhy) {
        write(newPhy, value, CPUstate);
      }
    }
  }
  
  // 5. if squash, recover the PRF state to the branch instruction
  if (input.recovery.active) {
    RobTag tag = input.recovery.nextTag;
    for (unsigned lane = 0; lane < RECOVERY_WIDTH; ++lane) {
      const unsigned offset = unsigned(input.recovery.offset) + lane;
      if (offset < ROB_CAP && offset < input.recovery.liveCount &&
          input.replay[lane].tag == tag &&
          ROB::isYounger(tag, input.recovery.squashTag)) {
        const auto phy = input.replay[lane].newPhy;
        if (phy != InvalidPhy) push(phy, writeTail, CPUstate);
      }
      tag = robNextTag(tag);
    }
  } else if (!input.squashDetect.needSquash) {
    // 6. if not, distribute a free PRF to the logic register
    if (input.issuePacket.valid) {
      if (input.issuePacket.allocDest) {
        auto headphy = pop(CPUstate);
        assert(headphy == input.issuePacket.phy);
        if (input.issuePacket.isControl) {
          write(input.issuePacket.phy, input.issuePacket.pc + 4, CPUstate);
        }
      }
    }
  }
  // 7. release the old physical register
  if (input.ROBModule.willCommit(input.squashDetect)) {
    int headIdx = robSlot(input.ROBModule.getHead());
    if (!input.ROBModule.isHeadHalt() &&
        (input.ROBModule.headType() == ROBType::REGISTER ||
         input.ROBModule.headType() == ROBType::LINK)) {
      int oldPhy = input.ROBModule.getOldPhy(headIdx);
      if (oldPhy != InvalidPhy)
        push(oldPhy, writeTail, CPUstate);
    }
  }
}
