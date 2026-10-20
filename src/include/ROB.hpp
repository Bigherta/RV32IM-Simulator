#pragma once
#include "BRU.hpp"
#include "SQ.hpp"
#include "common.hpp"
#include <cstdint>
#include <cstring>
enum class ROBType {
  REGISTER,
  BRANCH,
  STORE,
  LINK,
};
struct ROBEntry {
  ROBType type = ROBType::REGISTER;
  bool isCommitReady = false;
  uint8_t tag = 0xFF;
  int dest = 0; // if type is REGISTER, record its destination
  uint32_t predictedPC = 0;
  int32_t pc = 0;
  bool halt = false;
  bool exception = false;
  bool isRet = false;      // RISC-V RAS pop hint
  bool isCall = false;     // RISC-V RAS push hint
  bool isIndirect = false; // JALR variant: target from reg, not static imm
  uint8_t lqTailSnapshot = 0;
  uint8_t sqTailSnapshot = 0;
  uint8_t ckptId = 0;
  int newPhy = InvalidPhy; // InvalidPhy = instruction allocates no destination
  int oldPhy = InvalidPhy; // InvalidPhy = no mapping to free on commit
};
struct IssuePacket;
struct ROBInput {
  CommitPlan commits;
  SquashInfo squashDetect;
  aluCDB cdbOfALU;
  sharedCDB cdbOfShared;
  const BRU &BRUModule;
  StoreCompletion storeCompletion;
  const IssuePacket &issuePacket;
  ROBInput(const BRU &bru, const IssuePacket &pkt)
      : BRUModule(bru), issuePacket(pkt) {}
};
class ROB {
private:
  ROBEntry ROBqueue[ROB_CAP];
  uint8_t head = 0;
  uint8_t next_tag = 0;
  bool haltCommitted = false;
  bool trapCommitted = false;
  int haltRd = -1;
  void updateNextTag(systemState &CPUstate) const;
  int push(ROBEntry entry, systemState &CPUstate) const;
  void pop(systemState &CPUstate) const;
  void flush(RobTag squashTag, systemState &CPUstate) const;

public:
  static bool isOlder(RobTag tag_a, RobTag tag_b);
  static bool isYounger(RobTag tag_a, RobTag tag_b);
  bool isFull() const;
  bool isEmpty() const;
  bool matchesTag(RobTag tag) const;
  bool willCommit(const SquashInfo &squash) const;
  CommitPlan planCommit(const SquashInfo &squash, const SQ &sq) const;
  bool storeWillCommit(const SquashInfo &squash) const;
  bool isHaltCommitted() const;
  bool isTrapCommitted() const { return trapCommitted; }
  int getHaltRd() const;
  bool isHeadCommitReady() const;
  bool isHeadHalt() const;
  ROBType headType() const;
  int headDest() const;
  uint8_t getNextTag() const;
  int getHead() const;
  bool isCommitReadyAt(int index) const;
  ROBType getType(int index) const;
  int32_t getPC(int index) const;
  bool isHalt(int index) const;
  uint8_t getCkptId(int index) const;
  int getPredictedPC(int index) const;
  uint8_t getLqTailSnapshot(int index) const;
  uint8_t getSqTailSnapshot(int index) const;
  uint8_t getNewPhy(int index) const;
  uint8_t getOldPhy(int index) const;
  uint8_t getRd(int index) const;
  uint8_t getTag(int index) const { return ROBqueue[index].tag; }
  bool isRet(int index) const;
  bool isCall(int index) const;
  bool isIndirect(int index) const;
  RecoveryWindow recoveryWindow(unsigned offset) const;
  void tick(const ROBInput &, systemState &);
};
