#include "../include/ALU.hpp"
#include "../include/CPU.hpp"
#include "../include/common.hpp"
#include <cstdint>
void ALU::push(uint32_t op1, uint32_t op2, Operation op, RobTag robTag,
               bool isControl, bool keep, unsigned unit, const ALUInput &input, systemState &CPUstate) const {
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
  // Derive post-release availability from this/Input, never from the write
  // target. This keeps SAME-cycle drain/cancel-and-refill without next reads.
  const bool flushed = slotValid[unit] && input.squashDetect.needSquash &&
      !ROB::isOlder(outputBuffer[unit].robTag, input.squashDetect.SquashTag);
  if (!slotValid[unit] || input.popReady[unit] || flushed) {
    CPUstate.ALUModule.outputBuffer[unit] = result;
    CPUstate.ALUModule.slotValid[unit] = keep;
  }
}

uint32_t ALU::headValue(unsigned unit) const { return !isEmpty(unit) ? outputBuffer[unit].value : 0; }
uint8_t ALU::headRobTag(unsigned unit) const { return !isEmpty(unit) ? outputBuffer[unit].robTag : 0; }
bool ALU::headIsControl(unsigned unit) const { return !isEmpty(unit) && outputBuffer[unit].isControl; }
bool ALU::isFull(unsigned unit) const { return unit < ALU_UNITS && slotValid[unit]; }
bool ALU::isEmpty(unsigned unit) const { return !isFull(unit); }

void ALU::remove(uint8_t robTag, systemState &CPUstate) const {
  for (int i = 0; i < ALU_UNITS; i++) {
    if (slotValid[i] && outputBuffer[i].robTag == robTag) {
      CPUstate.ALUModule.slotValid[i] = false;
      return;
    }
  }
}

void ALU::flush(uint8_t tag, systemState &CPUstate) const {
  for (int i = 0; i < ALU_UNITS; i++) {
    if (slotValid[i] && !ROB::isOlder(outputBuffer[i].robTag, tag))
      CPUstate.ALUModule.slotValid[i] = false;
  }
}
void ALU::tick(const ALUInput &input, systemState &CPUstate) {
  // ALU writeBack: consume this unit's own grant on the CDB result.
  for (unsigned unit = 0; unit < ALU_UNITS; ++unit)
    if (input.popReady[unit]) remove(headRobTag(unit), CPUstate);
  // clear the wrong ALU outputBuffer
  if (input.squashDetect.needSquash) {
    flush(input.squashDetect.SquashTag, CPUstate);
  }
  for (unsigned unit = 0; unit < ALU_UNITS; ++unit) {
    const auto &dispatch = unit == 0 ? input.dispatch : input.dispatchSecond;
    if (dispatch.valid) {
    const auto &rs = input.RSModule.integerRS[dispatch.rsIndex];
    const bool keep = !input.squashDetect.needSquash ||
                      ROB::isOlder(dispatch.robTag, input.squashDetect.SquashTag);
    push(
        static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src1, input.operandWriteback)),
        static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src2, input.operandWriteback)),
        rs.op, dispatch.robTag, isControlOp(rs.op), keep, unit, input, CPUstate);
    }
  }
}
