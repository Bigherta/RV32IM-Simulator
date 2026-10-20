#pragma once
#include "ROB.hpp"
#include "SRAM.hpp"
#include "common.hpp"
#include <cstdint>
#include <cstring>

struct OperandInfo {
  bool ready;
  int32_t value;
  int phyRegIndex;
};
struct IssuePacket;
struct RATInput {
  CommitPlan commits;
  const ROB &ROBModule;
  const IssuePacket &issuePacket;
  SquashInfo squashDetect;
  RecoveryStep recovery;
  std::array<RecoveryNarrowEntry, RECOVERY_WIDTH> replay{};
  RATInput(const ROB &rob, const IssuePacket &pkt)
      : ROBModule(rob), issuePacket(pkt) {}
};

struct systemState;
class RAT {
private:
  uint8_t specRAT[REGISTER_CAP];
  SRAM<1, 192, 6> archStorage;
  uint32_t archValid = 0;
  bool replayOpen = false;
  void setSpecRAT(int regNum, int PRF_id);

public:
  RAT() {
    // InvalidPhy (=0) = unmapped; x0 is never renamed, P1-P31 bind to Px at
    // reset
    std::memset(specRAT, 0, sizeof(specRAT));
    for (int i = 1; i < 32; i++) {
      specRAT[i] = i;
    }
  }
  int readRAT(int regNum) const;
  OperandInfo readOperand(int regNum) const;
  void tick(const RATInput &, systemState &);
};
