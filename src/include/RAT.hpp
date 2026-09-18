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
  const IssuePacket &issuePacket;
  SquashInfo squashDetect;
  RATInput(const IssuePacket &pkt) : issuePacket(pkt) {}
};

struct systemState;
class RAT {
private:
  int RAT_PRF[REGISTER_CAP];
  RATSnapshot ratCkpt[CKPT_CAP];
  void setRAT_PRF(int regNum, int PRF_id);
  void restoreRAT_PRF(const RATSnapshot &snapshot);

public:
  RAT() {
    // InvalidPhy (=0) = unmapped; x0 is never renamed, P1-P31 bind to Px at reset
    std::memset(RAT_PRF, 0, sizeof(RAT_PRF));
    for (int i = 1; i < 32; i++)
      RAT_PRF[i] = i;
  }
  int readRAT_PRF(int regNum) const;
  OperandInfo readOperand(int regNum) const;
  void tick(const RATInput &, systemState &);
};
