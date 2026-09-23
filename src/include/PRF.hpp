#pragma once
#include "ROB.hpp"
#include "common.hpp"
#include <cstdint>
struct IssuePacket;
struct PRFEntry {
  int32_t value;
  bool ready;
};
struct PRFInput {
  const IssuePacket &issuePacket;
  SquashInfo squashDetect;
  aluCDB cdbOfALU;
  lqCDB cdbOfLQ;
  mulCDB cdbOfMul;
  divCDB cdbOfDiv;
  const ROB &ROBModule;
  PRFInput(const ROB &rob, const IssuePacket &pkt)
      : ROBModule(rob), issuePacket(pkt) {}
};
struct systemState;
class PRF {
private:
  PRFEntry PhysicalRegs[PRF_CAP];
  uint8_t freeList[PRF_CAP];
  PrfSeq headSeq = 0;
  PrfSeq tailSeq = 0;
  uint8_t pop();
  void push(int index);
  void write(int index, int32_t value);

public:
  PRF();
  bool isFreeListEmpty() const;
  PrfSeq getHeadSeq() const;
  uint8_t getFreeListSlot(PrfSeq seq) const { return freeList[prfSlot(seq)]; }
  bool isReady(int index) const;
  int32_t getValue(int index) const;
  // Operand resolves either a physical register (tag != InvalidPhy) or an
  // immediate constant (tag == InvalidPhy, value in imm). RS entries no longer
  // cache values; readiness and value come straight from the PRF / the
  // constant. Sentinel relies on the P0-dead invariant: real tags are
  // always 1..PRF_CAP-1, never 0.
  bool isOperandReady(const Operand &op) const {
    return op.tag == InvalidPhy || isReady(op.tag);
  }
  int32_t getOperandValue(const Operand &op) const {
    return op.tag == InvalidPhy ? op.imm : getValue(op.tag);
  }
  void tick(const PRFInput &, systemState &);
};
