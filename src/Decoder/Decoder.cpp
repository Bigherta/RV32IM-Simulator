#include "../include/Decoder.hpp"
#include "../include/CPU.hpp"
#include <cstring>

Uop Decoder::decode(int32_t raw_inst) {
  Uop inst;
  uint32_t raw = static_cast<uint32_t>(raw_inst);
  auto opcode = raw & 0x7F;
  inst.opcode = opcode;
  inst.funct3 = (raw >> 12) & 0x7;
  inst.rd = (raw >> 7) & 0x1F;
  inst.rs1 = (raw >> 15) & 0x1F;
  inst.rs2 = (raw >> 20) & 0x1F;

  auto getImm = [&](RISC_V type) -> int32_t {
    switch (type) {
    case RISC_V::Istar:
      return static_cast<int32_t>((raw >> 20) & 0x1F);
    case RISC_V::I:
      return signExtend(static_cast<int32_t>((raw >> 20) & 0xFFF), 12);
    case RISC_V::S:
      return signExtend(
          static_cast<int32_t>(((raw >> 7) & 0x1F) | ((raw >> 25) << 5)), 12);
    case RISC_V::B:
      return signExtend(static_cast<int32_t>(
                            ((raw >> 31) << 12) | (((raw >> 25) & 0x3F) << 5) |
                            ((inst.rd & 1) << 11) | ((inst.rd >> 1) << 1)),
                        13);
    case RISC_V::J:
      return signExtend(static_cast<int32_t>(((raw >> 31) << 20) |
                                             (((raw >> 21) & 0x3FF) << 1) |
                                             (((raw << 11) >> 31) << 11) |
                                             (((raw << 12) >> 24) << 12)),
                        21);
    case RISC_V::U:
      return static_cast<int32_t>(raw_inst & 0xFFFFF000);
    default:
      return 0;
    }
  };

  switch (opcode) {
  case 0b0110011: {
    inst.funct7 = (raw >> 25) & 0x7F;
    if (inst.funct7 == 0b0000001) {
      inst.type = RISC_V::M;
    } else {
      inst.type = RISC_V::R;
    }
    break;
  }
  case 0b0010011: {
    inst.funct7 = (raw >> 25) & 0x7F;
    auto link_funct = (inst.funct3 << 7) | inst.funct7;
    if (link_funct == 0b0010000000 || link_funct == 0b1010000000 ||
        link_funct == 0b1010100000) {
      inst.type = RISC_V::Istar;
    } else {
      inst.type = RISC_V::I;
    }
    inst.imm = getImm(inst.type);
    break;
  }
  case 0b0000011:
  case 0b1100111: {
    inst.type = RISC_V::I;
    inst.imm = getImm(RISC_V::I);
    break;
  }
  case 0b0100011: {
    inst.type = RISC_V::S;
    inst.imm = getImm(RISC_V::S);
    break;
  }
  case 0b1100011: {
    inst.type = RISC_V::B;
    inst.imm = getImm(RISC_V::B);
    break;
  }
  case 0b1101111: {
    inst.type = RISC_V::J;
    inst.imm = getImm(RISC_V::J);
    break;
  }
  case 0b0010111:
  case 0b0110111: {
    inst.type = RISC_V::U;
    inst.imm = getImm(RISC_V::U);
    break;
  }
  default:
    inst.type = RISC_V::RV_INVALID;
    break;
  }
  if (inst.type != RISC_V::S && inst.type != RISC_V::B && inst.rd != 0) {
    inst.allocDest = true;
  }
  return inst;
}

bool UopQueue::isEmpty() const { return head == tail; }

bool UopQueue::isFull() const { return ((tail + 1) & (IQ_CAP - 1)) == head; }

void UopQueue::push(Uop inst) {
  uopQueueEntries[tail] = inst;
  tail = (tail + 1) & (IQ_CAP - 1);
}
void UopQueue::pop() { head = (head + 1) & (IQ_CAP - 1); }
uint8_t UopQueue::getHead() const { return head; }
uint8_t UopQueue::getTail() const { return tail; }
void UopQueue::clear() {
  std::memset(this, 0, sizeof(*this));
  head = tail = 0;
}

void DecodeUnit::tick(const DecodeInput &input, systemState &CPUstate) {
  if (input.squashDetect.needSquash) {
    CPUstate.DecodeUnitModule.clear();
    return;
  }
  if (input.issuePacket.valid)
    CPUstate.DecodeUnitModule.pop();
  if (input.FQModule.isEmpty() || isFull())
    return;
  auto raw = input.FQModule.headRaw();
  Uop uop = Decoder::decode(static_cast<int32_t>(raw));
  uop.pc = input.FQModule.headpc();
  uop.predictedPC = input.FQModule.headPredictedPC();
  uop.ckptId = input.FQModule.headCkptId();
  uop.isHalt = (raw == 0x0ff00513);
  CPUstate.DecodeUnitModule.push(uop);
}