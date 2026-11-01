#pragma once
#include "ROB.hpp"
#include "SRAM.hpp"
#include "common.hpp"
#include <cstdint>
struct IssueGroup;
struct PRFEntry {
  bool ready;
  bool everWritten = false;
};
constexpr unsigned PRF_VALUE_WRITERS = ISSUE_WIDTH + WRITEBACK_PORTS;
struct PRFWriteRecord {
  bool valid = false;
  uint8_t phy = 0;
  uint32_t value = 0;
};
struct PRFInput {
  bool freeze = false;
  CommitPlan commits;
  const IssueGroup &issuePacket;
  SquashInfo squashDetect;
  RecoveryStep recovery;
  std::array<RecoveryNarrowEntry, RECOVERY_WIDTH> replay{};
  aluCDB cdbOfALU;
  aluCDB cdbOfALUSecond;
  sharedCDB cdbOfShared;
  const ROB &ROBModule;
  PRFInput(const ROB &rob, const IssueGroup &pkt)
      : ROBModule(rob), issuePacket(pkt) {}
};
struct systemState;
class PRF {
private:
  PRFEntry PhysicalRegs[PRF_CAP];
  SRAM<1, 32> payload[PRF_CAP - 1]; // P0 has no storage bank.
  std::array<PRFWriteRecord, PRF_VALUE_WRITERS> priorWrites{};
  uint8_t freeList[PRF_CAP];
  PrfSeq headSeq = 0;
  PrfSeq tailSeq = 0;
  void push(int index, PrfSeq &writeTail, systemState &CPUstate) const;

public:
  PRF();
  bool isFreeListEmpty() const;
  PrfSeq getHeadSeq() const;
  unsigned freeCount() const { return prfSeqDistance(headSeq, tailSeq); }
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
  bool isOperandReady(const Operand &op, const OperandWriteback &ports) const {
    bool ready = isOperandReady(op);
    for (const auto &port : ports)
      ready |= op.tag != InvalidPhy && port.valid && port.tag == op.tag;
    return ready;
  }
  int32_t getOperandValue(const Operand &op, const OperandWriteback &ports) const {
    if (isOperandReady(op)) return getOperandValue(op);
    for (const auto &port : ports)
      if (port.valid && port.tag == op.tag && op.tag != InvalidPhy)
        return port.value;
    return getOperandValue(op);
  }
  void tick(const PRFInput &, systemState &);
};
