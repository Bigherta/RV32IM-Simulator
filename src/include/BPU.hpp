#pragma once
#include "common.hpp"
#include <cstdint>
#include <cstring>

struct BRU;
struct ROB;
struct systemState;
// BP update arbitration input: both table-training sources (BRU branch results
// and CDB JAL/JALR transfers) converge to this single point so that the two
// update calls keep a fixed order (BRU candidate first) regardless of stage
// scheduling order.
struct BPUInput {
  const BRU &BRUModule;
  const ROB &ROBModule;
  aluCDB cdbOut;
  SquashInfo squashDetect;
  FetchDecision fetchDecision;
  FetchTypeInfo fetchInfo;
  BPUInput(const BRU &bru, const ROB &rob) : BRUModule(bru), ROBModule(rob) {}
};
// Tournament direction predictor: direct-PC local counters, gshare global
// counters, and a gshare-indexed chooser. All tables use the tightened 256-row
// capacity; counter value 1 is weakly not-taken and 2 is weakly taken.
struct DirectionPred {
  uint8_t localPHT[BHT_CAP] = {};
  uint8_t globalPHT[BHT_CAP] = {};
  uint8_t selector[SELECTOR_CAP] = {};
  uint8_t GHR = 0;
  DirectionPred() {
    std::memset(localPHT, 1, sizeof(localPHT));
    std::memset(globalPHT, 1, sizeof(globalPHT));
    std::memset(selector, 1, sizeof(selector));
  }
};

// Target prediction ("where to jump"): a committed RAS baseline and its
// speculative copy. The tops are non-wrapping depths in [0, RAS_CAP].
struct TargetPred {
  BTBEntry BTB[BTB_CAP] = {};
  uint32_t specRAS[RAS_CAP] = {};
  uint32_t archRAS[RAS_CAP] = {};
  uint8_t specTopOfRAS = 0;
  uint8_t archTopOfRAS = 0;
  bool condSeen[CONDSEEN_CAP] = {};
};

class BPU {
private:
  struct Cand {
    bool valid = false;
    uint32_t pc = 0;
    bool taken = false;
    uint32_t target = 0;
    uint8_t ghr = 0;
    bool cond = true;
    bool isRet = false;
  };
  DirectionPred dir;
  TargetPred tgt;
  uint8_t GHRCheckpoint[CKPT_CAP] = {};
  uint8_t nextCkptId = 0;
  uint64_t branchTotal = 0;
  uint64_t branchCorrect = 0;
  // Per-class prediction counters. Class split:
  //   cond = BRU-resolved conditional branches (site 1 in BPU::tick),
  //   jal  = direct JAL transfers on the ALU CDB (ROB isIndirect == false),
  //   jalr = indirect JALR transfers on the ALU CDB (ROB isIndirect == true).
  // Invariant: branch{Total,Correct} == the sum of the three classes.
  uint64_t condTotal = 0;
  uint64_t condCorrect = 0;
  uint64_t jalTotal = 0;
  uint64_t jalCorrect = 0;
  uint64_t jalrTotal = 0;
  uint64_t jalrCorrect = 0;
  // Debug-only per-PC misprediction counters (direct-mapped by pc[11:2]).
  // Synthesis strips these along with the VERBOSE topic.
  uint64_t missCnt[BTB_CAP] = {};
  uint32_t missPC[BTB_CAP] = {}; // sample PC per slot (last writer wins)
  uint8_t maxSpecTopOfRAS = 0;
  uint8_t maxArchTopOfRAS = 0;
  void noteMiss(uint32_t pc) {
    const auto i = (pc >> 2) & (BTB_CAP - 1);
    ++missCnt[i];
    missPC[i] = pc;
  }

  void update(uint32_t pc, bool taken, uint32_t target, uint8_t ghr);
  void updateJump(uint32_t pc, uint32_t target, bool isRet);
  void shiftGHR(bool taken);
public:
  uint64_t getBranchTotal() const { return branchTotal; }
  uint64_t getBranchCorrect() const { return branchCorrect; }
  uint64_t getCondTotal() const { return condTotal; }
  uint64_t getCondCorrect() const { return condCorrect; }
  uint64_t getJalTotal() const { return jalTotal; }
  uint64_t getJalCorrect() const { return jalCorrect; }
  uint64_t getJalrTotal() const { return jalrTotal; }
  uint64_t getJalrCorrect() const { return jalrCorrect; }
  uint8_t getMaxSpecTopOfRAS() const { return maxSpecTopOfRAS; }
  uint8_t getMaxArchTopOfRAS() const { return maxArchTopOfRAS; }
  void dumpBpMiss() const;
  PredictInfo predict(uint32_t pc) const;

  uint8_t snapshotCheckPoint() const;
  void recoverCheckPoint(const uint8_t);
  uint8_t getNextCkptId() const { return nextCkptId; }
  void tick(const BPUInput &, systemState &);
};
