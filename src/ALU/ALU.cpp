#include "../include/ALU.hpp"
#include "../include/CPU.hpp"
#include "../include/common.hpp"
#include <cstdint>
void ALU::push(uint32_t op1, uint32_t op2, Operation op, RobTag robTag,
               bool isControl) {
  // Operands and the result are uint32 bit vectors (RTL semantics).
  // Signedness is selected by the instruction: only SLT/SLTI and SRA
  // interpret the operand as int32_t; every other op is pure bit-vector
  // arithmetic.
  uint32_t value;
  if (isControlOp(op)) {
    // JALR clears the target's bit 0 (RISC-V: (rs1 + imm) & ~1). J-type
    // imm bit0 is always 0, so the same mask is harmless for JAL.
    value = (op1 + op2) & 0xFFFFFFFEu;
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
      value = op1 << (op2 & 0x1F);
      break;
    case Operation::SRL:
      value = op1 >> (op2 & 0x1F);
      break;
    case Operation::SRA:
      // C++20: right shift of a negative signed value is arithmetic.
      value = static_cast<uint32_t>(static_cast<int32_t>(op1) >> (op2 & 0x1F));
      break;
    case Operation::SLT:
      value = static_cast<int32_t>(op1) < static_cast<int32_t>(op2) ? 1u : 0u;
      break;
    case Operation::SLTU:
      value = op1 < op2 ? 1u : 0u;
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

uint32_t ALU::headValue() const {
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
    CPUstate.ALUModule.push(
        static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src1)),
        static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src2)),
        rs.op, input.dispatch.robTag, isControlOp(rs.op));
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