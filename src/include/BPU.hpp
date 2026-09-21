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

// SARAS correction queue entry: the address, its LIFO position, and the
// times counter before the speculative action (so both pops and
// times inc/dec are undoable). One entry is recorded for every
// speculative call-dedup and every speculative ret.
struct AlignEntry {
  uint32_t addr;
  uint8_t index;
  uint32_t times;
};

struct RASEntry {
  uint32_t retPC;
  uint32_t times;
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

// Target prediction ("where to jump"): BTB (targets + jump type) and the
// SARAS ring return-address stack with its correction queue. Its ring
// counters are uint8_t and wrap at 256, well beyond the current
// ROB_CAP=16 and local queue capacities (ALIGNQ_CAP=16/RAS_CAP=8).
struct TargetPred {
  BTBEntry BTB[BTB_CAP] = {};
  RASEntry RAS[RAS_CAP] = {};
  uint8_t RAS_top = 0; // ring write pointer (wraps at 256)
  AlignEntry alignQueue[ALIGNQ_CAP] = {};
  uint8_t alignTail = 0; // AlignQueue tail (appended on CALL-dedup / RET)
  // Branch-type filter: set when a PC resolves as a conditional (taken or
  // not). Lets the fetch stage shift the GHR for conditionals that are not
  // BTB-resident (never-taken branches never train the BTB), so history
  // membership stops depending on BTB residency churn.
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
  BPUSnapshot bpCkpt[CKPT_CAP] = {};
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
  void dumpBpMiss() const;
  PredictInfo predict(uint32_t pc) const;

  BPUSnapshot snapshotCheckPoint() const;
  void recoverCheckPoint(const BPUSnapshot &);
  uint8_t getNextCkptId() const { return nextCkptId; }
  void tick(const BPUInput &, systemState &);
};
