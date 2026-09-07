#pragma once
#include "common.h"
#include "tools.h"
#include <array>
#include <cstdint>

static_assert(IQ_CAP == 16);   // head/tail are 4-bit ring pointers
static_assert(CKPT_CAP == 64); // ckptId is a 6-bit checkpoint index

class Decoder {
public:
  static Uop decode(int32_t raw_inst);
  static inline int32_t signExtend(int32_t raw_data, int len) {
    return (raw_data << (32 - len)) >> (32 - len);
  }
};

struct DecodeInput {
  Wire<1> needSquash;
  Wire<1> issueValid;
  Wire<1> fqEmpty;
  Wire<32> fqHeadRaw;
  Wire<32> fqHeadPc;
  Wire<32> fqHeadPredictedPC;
  Wire<6> fqHeadCkptId;
};

struct UopEntry {
  Register<4> type; // RISC_V (R..RV_INVALID, 9 values)
  Register<7> opcode;
  Register<3> funct3;
  Register<7> funct7;
  Register<5> rd;
  Register<5> rs1;
  Register<5> rs2;
  Register<32> imm;
  Register<32> pc;
  Register<1> isHalt;
  Register<1> allocDest;
  Register<32> predictedPC;
  Register<6> ckptId;
};

struct IQOutput {
  Register<4> head;
  Register<4> tail;
};

struct IQInner {
  std::array<UopEntry, IQ_CAP> entries;
};

// Decoded-instruction queue (formerly InstructQueue wrapped by DecodeUnit).
struct DecodeUnit : dark::Module<DecodeInput, IQOutput, IQInner> {
  void work() override;

  // bridge accessors: combinational reads of the committed (_M_old) state
  bool isEmpty() const {
    return static_cast<uint32_t>(head) == static_cast<uint32_t>(tail);
  }
  bool isFull() const {
    return ((static_cast<uint32_t>(tail) + 1) & (IQ_CAP - 1)) ==
           static_cast<uint32_t>(head);
  }
  uint8_t getHead() const {
    return static_cast<uint8_t>(static_cast<uint32_t>(head));
  }
  uint8_t getTail() const {
    return static_cast<uint8_t>(static_cast<uint32_t>(tail));
  }
  RISC_V headType() const {
    return static_cast<RISC_V>(
        static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].type));
  }
  int headOpcode() const {
    return static_cast<int>(
        static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].opcode));
  }
  int headFunct3() const {
    return static_cast<int>(
        static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].funct3));
  }
  int headFunct7() const {
    return static_cast<int>(
        static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].funct7));
  }
  int headRd() const {
    return static_cast<int>(
        static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].rd));
  }
  int headRs1() const {
    return static_cast<int>(
        static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].rs1));
  }
  int headRs2() const {
    return static_cast<int>(
        static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].rs2));
  }
  int32_t headImm() const {
    return static_cast<int32_t>(
        static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].imm));
  }
  uint32_t headPc() const {
    return static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].pc);
  }
  bool headIsHalt() const {
    return static_cast<bool>(entries[static_cast<uint32_t>(head)].isHalt);
  }
  bool headAllocDest() const {
    return static_cast<bool>(entries[static_cast<uint32_t>(head)].allocDest);
  }
  int32_t headPredictedPC() const {
    return static_cast<int32_t>(static_cast<uint32_t>(
        entries[static_cast<uint32_t>(head)].predictedPC));
  }
  uint8_t headCkptId() const {
    return static_cast<uint8_t>(static_cast<uint32_t>(
        entries[static_cast<uint32_t>(head)].ckptId));
  }
};
