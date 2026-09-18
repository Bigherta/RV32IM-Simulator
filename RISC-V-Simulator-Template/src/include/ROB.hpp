#pragma once
#include "common.h"
#include "concept.h"
#include "module.h"
#include "tools.h"
#include <array>
#include <cstdint>
#include <cstring>

enum class ROBType : dark::max_size_t {
  REGISTER,
  BRANCH,
  STORE,
  LINK,
};
// ---- Input ----
struct ROBInputSquash {
  Wire<1> needSquash;
  Wire<7> SquashTag;
};
struct ROBInputEntry {
  Wire<2> type;
  Wire<1> isCommitReady;
  Wire<5> dest;
  Wire<1> halt;
  Wire<1> isCall;
  Wire<1> isRet;
  Wire<6> ckptId;
  Wire<32> predictedPC;
  Wire<32> pc;
  Wire<4> lqTailSnapshot;
  Wire<4> sqTailSnapshot;
  Wire<7> newPhy;
  Wire<7> oldPhy;
};
struct ROBInputIssue {
  Wire<1> issueValid;
  ROBInputEntry entry;
};
// Quad-CDB commit-ready ports (mirrors the main tree's
// ROBInput.cdbOfALU/cdbOfLQ/cdbOfMul/cdbOfDiv): each bus marks its result's
// ROB entry commit-ready; only valid+tag are needed (the ROB reads no
// payload).
struct ROBInputCDBAlu {
  Wire<1> cdbValid;
  Wire<7> cdbRobTag;
};
struct ROBInputCDBLq {
  Wire<1> cdbValid;
  Wire<7> cdbRobTag;
};
struct ROBInputCDBMul {
  Wire<1> cdbValid;
  Wire<7> cdbRobTag;
};
struct ROBInputCDBDiv {
  Wire<1> cdbValid;
  Wire<7> cdbRobTag;
};
struct ROBInputBRU {
  Wire<1> isBRUEmpty;
  Wire<7> bruHeadRobTag;
};
struct ROBInputSQ {
  std::array<Wire<1>, SQ_CAP> sqValid;
  std::array<Wire<1>, SQ_CAP> sqReadyToCommit;
  std::array<Wire<7>, SQ_CAP> sqRobTag;
  Wire<4> sqHead;
};
struct ROBInput {
  ROBInputSquash squash;
  ROBInputIssue issue;
  ROBInputCDBAlu cdbOfALU;
  ROBInputCDBLq cdbOfLQ;
  ROBInputCDBMul cdbOfMUL;
  ROBInputCDBDiv cdbOfDIV;
  ROBInputBRU bru;
  ROBInputSQ sq;
};

// ---- Output: 10-scalar view (group name headView avoids base-class name-hiding
// with bridge accessor methods isEmpty/isFull/isHeadCommitReady/isHeadHalt/headType/headDest) + flat entry arrays ----
struct ROBOutputHeadView {
  Wire<7> head;
  Wire<1> isEmpty;
  Wire<1> isHeadCommitReady;
  Wire<1> isHeadHalt;
  Wire<2> headType;
};
struct ROBOutput {
  ROBOutputHeadView headView;
  // Per-entry views consumed outside the ROB.
  struct Entry {
    std::array<Wire<1>, ROB_CAP> isCommitReady;
    std::array<Wire<1>, ROB_CAP> isCall;
    std::array<Wire<1>, ROB_CAP> isRet;
    std::array<Wire<6>, ROB_CAP> ckptId;
    std::array<Wire<32>, ROB_CAP> predictedPC;
    std::array<Wire<32>, ROB_CAP> pc;
    std::array<Wire<4>, ROB_CAP> lqTailSnapshot;
    std::array<Wire<4>, ROB_CAP> sqTailSnapshot;
    std::array<Wire<7>, ROB_CAP> newPhy;
    std::array<Wire<7>, ROB_CAP> oldPhy;
  };
  Entry entry;
};

// Inner: flat ROBEntryReg array + state — keep in sync
struct ROBEntryReg {
  Register<2> type;
  Register<1> isCommitReady;
  Register<5> dest;
  Register<1> halt;
  Register<1> isCall;
  Register<1> isRet;
  Register<6> ckptId;
  Register<32> predictedPC;
  Register<32> pc;
  Register<4> lqTailSnapshot;
  Register<4> sqTailSnapshot;
  Register<7> newPhy;
  Register<7> oldPhy;
};
struct ROBInner {
  std::array<ROBEntryReg, ROB_CAP> ROBqueue;
  Register<7> robHead;
  Register<7> next;
  Register<1> robHaltCommitted;
  Register<5> robHaltRd;
};

struct ROB : dark::Module<ROBInput, ROBOutput, ROBInner> {
  friend struct ReorderTester;
private:
  void wire_output();
  void updateNextTag();
  void pop();
  void flush(uint32_t squashTag);
public:
  ROB();
  static bool isOlder(RobTag tag_a, RobTag tag_b);
  static bool isYounger(RobTag tag_a, RobTag tag_b);
  bool isFull() const;
  bool isEmpty() const;
  bool isHaltCommitted() const;
  uint32_t getHaltRd() const;
  uint32_t getNextTag() const;
  void work() override;
};
