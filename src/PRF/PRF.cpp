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
uint8_t PRF::pop() {
  if (isFreeListEmpty())
    throw std::runtime_error("PRF free list underflow!");
  uint8_t phy = freeList[prfSlot(headSeq)];
  assert(phy !=
         InvalidPhy); // P0-dead invariant: popped tags are real registers
  headSeq = prfSeqNext(headSeq);
  PhysicalRegs[phy].ready = false; // prevent reading the stale data
  return phy;
}

void PRF::push(int index) {
  assert(index != InvalidPhy); // P0-dead invariant: only real tags are recycled
  assert(prfSeqDistance(headSeq, tailSeq) <
         PRF_CAP); // free list never overflows
  freeList[prfSlot(tailSeq)] = index;
  tailSeq = prfSeqNext(tailSeq);
}

bool PRF::isFreeListEmpty() const { return headSeq == tailSeq; }

PrfSeq PRF::getHeadSeq() const { return headSeq; }

bool PRF::isReady(int index) const { return PhysicalRegs[index].ready; }
int32_t PRF::getValue(int index) const { return PhysicalRegs[index].value; }
void PRF::write(int index, int32_t value) {
  PhysicalRegs[index].ready = true;
  PhysicalRegs[index].value = value;
}

void PRF::tick(const PRFInput &input, systemState &CPUstate) {
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
          CPUstate.PRFModule.write(newPhy, value);
        }
      }
    }
  }
  // 2. cdb of lq wake up prf
  if (input.cdbOfLQ.valid && input.ROBModule.matchesTag(input.cdbOfLQ.robTag)) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfLQ.robTag, input.squashDetect.SquashTag)) {
      auto robIdx = robSlot(input.cdbOfLQ.robTag);
      auto value = input.cdbOfLQ.value;
      int newPhy = input.ROBModule.getNewPhy(robIdx);
      if (newPhy != InvalidPhy) {
        CPUstate.PRFModule.write(newPhy, value);
      }
    }
  }
  // 3. cdb of mul wake up prf
  if (input.cdbOfMul.valid &&
      input.ROBModule.matchesTag(input.cdbOfMul.robTag)) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfMul.robTag, input.squashDetect.SquashTag)) {
      auto robIdx = robSlot(input.cdbOfMul.robTag);
      auto value = input.cdbOfMul.value;
      int newPhy = input.ROBModule.getNewPhy(robIdx);
      if (newPhy != InvalidPhy) {
        CPUstate.PRFModule.write(newPhy, value);
      }
    }
  }
  // 4. cdb of div wake up prf
  if (input.cdbOfDiv.valid &&
      input.ROBModule.matchesTag(input.cdbOfDiv.robTag)) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfDiv.robTag, input.squashDetect.SquashTag)) {
      auto robIdx = robSlot(input.cdbOfDiv.robTag);
      auto value = input.cdbOfDiv.value;
      int newPhy = input.ROBModule.getNewPhy(robIdx);
      if (newPhy != InvalidPhy) {
        CPUstate.PRFModule.write(newPhy, value);
      }
    }
  }
  
  // 5. if squash, recover the PRF state to the branch instruction
  if (input.squashDetect.needSquash) {
    const RobTag oldNext = input.ROBModule.getNextTag();
    RobTag tag = robNextTag(input.squashDetect.SquashTag);
    bool scanDone = (tag == oldNext);
    for (int k = 0; k < ROB_CAP; ++k) {
      if (scanDone)
        continue;
      auto recoverPRF = input.ROBModule.getNewPhy(robSlot(tag));
      if (recoverPRF != InvalidPhy)
        CPUstate.PRFModule.push(recoverPRF);
      tag = robNextTag(tag);
      if (tag == oldNext)
        scanDone = true;
    }
  } else {
    // 6. if not, distribute a free PRF to the logic register
    if (input.issuePacket.valid) {
      if (input.issuePacket.allocDest) {
        auto headphy = CPUstate.PRFModule.pop();
        assert(headphy == input.issuePacket.phy);
        if (input.issuePacket.isControl) {
          CPUstate.PRFModule.write(input.issuePacket.phy,
                                   input.issuePacket.pc + 4);
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
        CPUstate.PRFModule.push(oldPhy);
    }
  }
}
