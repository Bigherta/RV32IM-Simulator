#include "../include/CPU.hpp"
#include <cstdint>

void BRU::BRUExecute(uint32_t op1, uint32_t op2, uint32_t pc, uint32_t imm,
                     Operation op, RobTag robTag, bool keep, systemState &CPUstate) const {
  bool taken = false;
  switch (op) {
  case Operation::EQ:
    taken = (op1 == op2);
    break;
  case Operation::NE:
    taken = (op1 != op2);
    break;
  case Operation::LT:
    taken = (static_cast<int32_t>(op1) < static_cast<int32_t>(op2));
    break;
  case Operation::GE:
    taken = (static_cast<int32_t>(op1) >= static_cast<int32_t>(op2));
    break;
  case Operation::LTU:
    taken = (op1 < op2);
    break;
  case Operation::GEU:
    taken = (op1 >= op2);
    break;
  default:
    taken = false;
    break;
  }
  // uint32 bit-vector add: the target wraps at 32 bits by construction.
  push({pc, taken ? pc + imm : pc + 4u, robTag}, keep, CPUstate);
}

void BRU::push(BranchResult result, bool keep, systemState &CPUstate) const {
  for (int i = 0; i < BRU_CAP; i++)
    if (!slotValid[i]) {
      CPUstate.BRUModule.outputBuffer[i] = result;
      CPUstate.BRUModule.slotValid[i] = keep;
      return;
    }
}

uint32_t BRU::headPCFrom() const {
  int best = -1;
  for (int i = 0; i < BRU_CAP; i++) {
    if (slotValid[i] &&
        (best == -1 ||
         ROB::isOlder(outputBuffer[i].robTag, outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].pcFrom : 0;
}
uint32_t BRU::headPCResult() const {
  int best = -1;
  for (int i = 0; i < BRU_CAP; i++) {
    if (slotValid[i] &&
        (best == -1 ||
         ROB::isOlder(outputBuffer[i].robTag, outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].pcResult : 0;
}
uint8_t BRU::headRobTag() const {
  int best = -1;
  for (int i = 0; i < BRU_CAP; i++) {
    if (slotValid[i] &&
        (best == -1 ||
         ROB::isOlder(outputBuffer[i].robTag, outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].robTag : 0;
}

bool BRU::isFull() const {
  for (int i = 0; i < BRU_CAP; i++) {
    if (!slotValid[i])
      return false;
  }
  return true;
}

bool BRU::isEmpty() const {
  for (int i = 0; i < BRU_CAP; i++) {
    if (slotValid[i])
      return false;
  }
  return true;
}

void BRU::remove(uint8_t robTag, systemState &CPUstate) const {
  for (int i = 0; i < BRU_CAP; i++) {
    if (slotValid[i] && outputBuffer[i].robTag == robTag) {
      CPUstate.BRUModule.slotValid[i] = false;
      return;
    }
  }
}

void BRU::flush(uint8_t tag, systemState &CPUstate) const {
  for (int i = 0; i < BRU_CAP; i++) {
    if (slotValid[i] && !ROB::isOlder(outputBuffer[i].robTag, tag))
      CPUstate.BRUModule.slotValid[i] = false;
  }
}

void BRU::tick(const BRUInput &input, systemState &CPUstate) {
  // stage 1: consume the dispatch bus (select was already evaluated on the
  // DispatchArbiter snapshot side; RS slot release is handled by RSUnit.tick)
  if (input.dispatch.valid) {
    auto &rs = input.RSModule.branchRS[input.dispatch.rsIndex];
    const bool keep = !input.squashDetect.needSquash ||
                      ROB::isOlder(input.dispatch.robTag, input.squashDetect.SquashTag);
    BRUExecute(
        static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src1)),
        static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src2)),
        static_cast<uint32_t>(rs.pc), static_cast<uint32_t>(rs.imm), rs.op,
        input.dispatch.robTag, keep, CPUstate);
  }
  // BRU writeBack
  if (!isEmpty()) {
    uint8_t brRobTag = headRobTag();
    remove(brRobTag, CPUstate);
  }
  // clear the wrong BRU outputBuffer
  if (input.squashDetect.needSquash) {
    flush(input.squashDetect.SquashTag, CPUstate);
  }
}
