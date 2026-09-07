#include "../include/Decoder.hpp"

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

void DecodeUnit::work() {
  // stage 3 flush first: on squash drop all decoded uops (head==tail makes
  // stale entries unreachable)
  if (needSquash) {
    head <= 0;
    tail <= 0;
  } else {
    // stage 2 pop: release the head uop once the issue arbiter consumed it
    if (static_cast<bool>(issueValid)) {
      head <= ((static_cast<uint32_t>(head) + 1) & (IQ_CAP - 1));
    }
    // stage 1 push: decode the InstructBuffer head when the queue can accept
    // it
    if (!static_cast<bool>(fqEmpty) && !isFull()) {
      auto raw = static_cast<uint32_t>(fqHeadRaw);
      Uop uop = Decoder::decode(static_cast<int32_t>(raw));
      uop.pc = static_cast<uint32_t>(fqHeadPc);
      uop.predictedPC =
          static_cast<int32_t>(static_cast<uint32_t>(fqHeadPredictedPC));
      uop.ckptId = static_cast<uint8_t>(static_cast<uint32_t>(fqHeadCkptId));
      uop.isHalt = (raw == 0x0ff00513);
      auto t = static_cast<uint32_t>(tail);
      entries[t].type <= static_cast<uint32_t>(uop.type);
      entries[t].opcode <= static_cast<uint32_t>(uop.opcode);
      entries[t].funct3 <= static_cast<uint32_t>(uop.funct3);
      entries[t].funct7 <= static_cast<uint32_t>(uop.funct7);
      entries[t].rd <= static_cast<uint32_t>(uop.rd);
      entries[t].rs1 <= static_cast<uint32_t>(uop.rs1);
      entries[t].rs2 <= static_cast<uint32_t>(uop.rs2);
      entries[t].imm <= static_cast<uint32_t>(uop.imm);
      entries[t].pc <= static_cast<uint32_t>(uop.pc);
      entries[t].isHalt <= static_cast<uint32_t>(uop.isHalt);
      entries[t].allocDest <= static_cast<uint32_t>(uop.allocDest);
      entries[t].predictedPC <= static_cast<uint32_t>(uop.predictedPC);
      entries[t].ckptId <= static_cast<uint32_t>(uop.ckptId);
      tail <= ((t + 1) & (IQ_CAP - 1));
    }
  }
}
