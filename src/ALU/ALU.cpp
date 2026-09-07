#include "../include/ALU.hpp"
#include "../include/CPU.hpp"
#include "../include/common.hpp"
#include <cstdint>
void ALU::push(int32_t op1, int32_t op2, Operation op, RobTag robTag,
               bool isControl) {
  int32_t value;
  if (isControlOp(op)) {
    value = op1 + op2;
  } else {
    switch (op) {
    case Operation::ADD:
    case Operation::AUIPC:
      value = op1 + op2;
      break;
    case Operation::SUB:
      value = op1 - op2;
      break;
    case Operation::XOR:
      value = op1 ^ op2;
      break;
    case Operation::OR:
      value = op1 | op2;
      break;
    case Operation::AND:
      value = op1 & op2;
      break;
    case Operation::SL:
      value = static_cast<int32_t>(static_cast<uint32_t>(op1) << (op2 & 0x1F));
      break;
    case Operation::SRL:
      value = static_cast<int32_t>(static_cast<uint32_t>(op1) >> (op2 & 0x1F));
      break;
    case Operation::SRA:
      value = op1 >> (op2 & 0x1F);
      break;
    case Operation::SLT:
      value = op1 < op2 ? 1 : 0;
      break;
    case Operation::SLTU:
      value = static_cast<uint32_t>(op1) < static_cast<uint32_t>(op2) ? 1 : 0;
      break;
    case Operation::LUI:
      value = op2;
      break;
    default:
      value = 0;
      break;
    }
  }
  ArithmeticCalculateResult result{};
  result.value = value;
  result.robTag = robTag;
  result.isControl = isControl;
  for (int i = 0; i < ALU_CAP; i++)
    if (!slotValid[i]) {
      outputBuffer[i] = result;
      slotValid[i] = true;
      return;
    }
}

int32_t ALU::headValue() const {
  int best = -1;
  for (int i = 0; i < ALU_CAP; i++) {
    if (slotValid[i] &&
        (best == -1 ||
         ROB::isOlder(outputBuffer[i].robTag, outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].value : 0;
}
uint8_t ALU::headRobTag() const {
  int best = -1;
  for (int i = 0; i < ALU_CAP; i++) {
    if (slotValid[i] &&
        (best == -1 ||
         ROB::isOlder(outputBuffer[i].robTag, outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].robTag : 0;
}
bool ALU::headIsControl() const {
  int best = -1;
  for (int i = 0; i < ALU_CAP; i++) {
    if (slotValid[i] &&
        (best == -1 ||
         ROB::isOlder(outputBuffer[i].robTag, outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].isControl : false;
}

bool ALU::isFull() const {
  for (int i = 0; i < ALU_CAP; i++) {
    if (!slotValid[i])
      return false;
  }
  return true;
}

bool ALU::isEmpty() const {
  for (int i = 0; i < ALU_CAP; i++) {
    if (slotValid[i])
      return false;
  }
  return true;
}

void ALU::remove(uint8_t robTag) {
  for (int i = 0; i < ALU_CAP; i++) {
    if (slotValid[i] && outputBuffer[i].robTag == robTag) {
      slotValid[i] = false;
      return;
    }
  }
}

void ALU::flush(uint8_t tag) {
  for (int i = 0; i < ALU_CAP; i++) {
    if (slotValid[i] && !ROB::isOlder(outputBuffer[i].robTag, tag))
      slotValid[i] = false;
  }
}
void ALU::tick(const ALUInput &input, systemState &CPUstate) {
  if (input.dispatch.valid) {
    auto &rs = input.RSModule.integerRS[input.dispatch.rsIndex];
    CPUstate.ALUModule.push(input.PRFModule.getOperandValue(rs.src1),
                            input.PRFModule.getOperandValue(rs.src2),
                            rs.op, input.dispatch.robTag,
                            isControlOp(rs.op));
  }
  // ALU writeBack: consume this unit's own grant on the CDB result.
  if (input.cdbOutput.valid) {
    CPUstate.ALUModule.remove(input.cdbOutput.robTag);
  }
  // clear the wrong ALU outputBuffer
  if (input.squashDetect.needSquash) {
    CPUstate.ALUModule.flush(input.squashDetect.SquashTag);
  }
}