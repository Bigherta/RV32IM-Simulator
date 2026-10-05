#pragma once
#include "InstructBuffer.hpp"
#include "common.hpp"
#include <cstdint>
class Decoder {
public:
  static Uop decode(int32_t raw_inst);
  static inline int32_t signExtend(int32_t raw_data, int len) {
    return (raw_data << (32 - len)) >> (32 - len);
  }
};

class UopQueue {
private:
  Uop uopQueueEntries[IQ_CAP];
  uint8_t head = 0;
  uint8_t tail = 0;

public:
  bool isEmpty() const;
  bool isFull() const;
  void push(Uop inst, systemState &CPUstate) const;
  RISC_V headType() const { return uopQueueEntries[head & (IQ_CAP - 1)].type; }
  int headOpcode() const { return uopQueueEntries[head & (IQ_CAP - 1)].opcode; }
  int headFunct3() const { return uopQueueEntries[head & (IQ_CAP - 1)].funct3; }
  int headFunct7() const { return uopQueueEntries[head & (IQ_CAP - 1)].funct7; }
  int headRd() const { return uopQueueEntries[head & (IQ_CAP - 1)].rd; }
  int headRs1() const { return uopQueueEntries[head & (IQ_CAP - 1)].rs1; }
  int headRs2() const { return uopQueueEntries[head & (IQ_CAP - 1)].rs2; }
  int32_t headImm() const { return uopQueueEntries[head & (IQ_CAP - 1)].imm; }
  uint32_t headPc() const { return uopQueueEntries[head & (IQ_CAP - 1)].pc; }
  bool headIsHalt() const { return uopQueueEntries[head & (IQ_CAP - 1)].isHalt; }
  bool headAllocDest() const { return uopQueueEntries[head & (IQ_CAP - 1)].allocDest; }
  int32_t headPredictedPC() const {
    return uopQueueEntries[head & (IQ_CAP - 1)].predictedPC;
  }
  uint8_t headCkptId() const { return uopQueueEntries[head & (IQ_CAP - 1)].ckptId; }
  void pop(systemState &CPUstate) const;
  void clear(systemState &CPUstate) const;
};
struct IssuePacket;
struct DecodeInput {
  SquashInfo squashDetect;
  const InstructBuffer &FQModule;
  const IssuePacket &issuePacket;
  DecodeInput(const InstructBuffer &fq, const IssuePacket &pkt)
      : FQModule(fq), issuePacket(pkt) {}
};
struct systemState;
class DecodeUnit {
  friend class UopQueue;
private:
  UopQueue iq;
  void push(Uop inst, systemState &CPUstate) const { iq.push(inst, CPUstate); }
  void pop(systemState &CPUstate) const { iq.pop(CPUstate); }
  void clear(systemState &CPUstate) const { iq.clear(CPUstate); }

public:
  void tick(const DecodeInput &input, systemState &CPUstate);
  bool isEmpty() const { return iq.isEmpty(); }
  bool isFull() const { return iq.isFull(); }
  bool canAccept(bool willPop) const { return !isFull() || (willPop && !isEmpty()); }
  RISC_V headType() const { return iq.headType(); }
  int headOpcode() const { return iq.headOpcode(); }
  int headFunct3() const { return iq.headFunct3(); }
  int headFunct7() const { return iq.headFunct7(); }
  int headRd() const { return iq.headRd(); }
  int headRs1() const { return iq.headRs1(); }
  int headRs2() const { return iq.headRs2(); }
  int32_t headImm() const { return iq.headImm(); }
  uint32_t headPc() const { return iq.headPc(); }
  bool headIsHalt() const { return iq.headIsHalt(); }
  bool headAllocDest() const { return iq.headAllocDest(); }
  int32_t headPredictedPC() const { return iq.headPredictedPC(); }
  uint8_t headCkptId() const { return iq.headCkptId(); }
};
