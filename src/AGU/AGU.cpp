#include "../include/AGU.hpp"
#include "../include/CPU.hpp"
#include <cstdint>
void AGU::push(uint32_t op1, uint32_t op2, RobTag robTag, uint8_t memIndex,
               bool keep, systemState &CPUstate) const {
  // uint32 bit-vector add: the address wraps at 32 bits by construction.
  const uint32_t value = op1 + op2;
  AddressCalculateResult result{value, robTag, memIndex};
  for (int i = 0; i < AGU_CAP; i++)
    if (!slotValid[i]) {
      CPUstate.AGUModule.outputBuffer[i] = result;
      CPUstate.AGUModule.slotValid[i] = keep;
      return;
    }
}

uint32_t AGU::headValue() const {
  int best = -1;
  for (int i = 0; i < AGU_CAP; i++) {
    if (slotValid[i] &&
        (best == -1 ||
         ROB::isOlder(outputBuffer[i].robTag, outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].value : 0;
}
uint8_t AGU::headRobTag() const {
  int best = -1;
  for (int i = 0; i < AGU_CAP; i++) {
    if (slotValid[i] &&
        (best == -1 ||
         ROB::isOlder(outputBuffer[i].robTag, outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].robTag : 0;
}
uint8_t AGU::headMemIndex() const {
  int best = -1;
  for (int i = 0; i < AGU_CAP; i++) {
    if (slotValid[i] &&
        (best == -1 ||
         ROB::isOlder(outputBuffer[i].robTag, outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].memIndex : 0;
}
bool AGU::isFull() const {
  for (int i = 0; i < AGU_CAP; i++) {
    if (!slotValid[i])
      return false;
  }
  return true;
}

bool AGU::isEmpty() const {
  for (int i = 0; i < AGU_CAP; i++) {
    if (slotValid[i])
      return false;
  }
  return true;
}

void AGU::remove(uint8_t robTag, systemState &CPUstate) const {
  for (int i = 0; i < AGU_CAP; i++) {
    if (slotValid[i] && outputBuffer[i].robTag == robTag) {
      CPUstate.AGUModule.slotValid[i] = false;
      return;
    }
  }
}

void AGU::flush(uint8_t tag, systemState &CPUstate) const {
  for (int i = 0; i < AGU_CAP; i++) {
    if (slotValid[i] && !ROB::isOlder(outputBuffer[i].robTag, tag))
      CPUstate.AGUModule.slotValid[i] = false;
  }
}
void AGU::tick(const AGUInput &input, systemState &CPUstate) {
  if (input.dispatch.valid) {
    const bool keep = !input.squashDetect.needSquash ||
                      ROB::isOlder(input.dispatch.robTag, input.squashDetect.SquashTag);
    if (input.dispatch.rsType == RSType::Load) {
      auto &rs = input.RSModule.loadRS[input.dispatch.rsIndex];
      push(
          static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src1)),
          static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src2)),
          input.dispatch.robTag, rs.memIndex, keep, CPUstate);
    } else {
      auto &rs = input.RSModule.storeAddressRS[input.dispatch.rsIndex];
      push(
          static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src1)),
          static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src2)),
          input.dispatch.robTag, rs.memIndex, keep, CPUstate);
    }
  }
  // AGU remove the first entry every cycle
  if (!isEmpty()) {
    remove(headRobTag(), CPUstate);
  }
  // clear the wrong AGU buffer
  if (input.squashDetect.needSquash) {
    flush(input.squashDetect.SquashTag, CPUstate);
  }
}
