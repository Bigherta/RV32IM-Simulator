#pragma once
#include "ROB.hpp"
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
  const ROB &ROBModule;
  const IssuePacket &issuePacket;
  SquashInfo squashDetect;
  RATInput(const ROB &rob, const IssuePacket &pkt)
      : ROBModule(rob), issuePacket(pkt) {}
};

struct systemState;
class RAT {
private:
  uint8_t specRAT[REGISTER_CAP];
  uint8_t archRAT[REGISTER_CAP];
  void setSpecRAT(int regNum, int PRF_id);
  void setArchRAT(int regNum, int PRF_id);
  void restoreRAT();

public:
  RAT() {
    // InvalidPhy (=0) = unmapped; x0 is never renamed, P1-P31 bind to Px at
    // reset
    std::memset(specRAT, 0, sizeof(specRAT));
    std::memset(archRAT, 0, sizeof(archRAT));
    for (int i = 1; i < 32; i++) {
      specRAT[i] = i;
      archRAT[i] = i;
    }
  }
  int readRAT(int regNum) const;
  OperandInfo readOperand(int regNum) const;
  void tick(const RATInput &, systemState &);
};
