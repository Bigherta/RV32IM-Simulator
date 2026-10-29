#pragma once
#include "common.hpp"
#include "SRAM.hpp"
#include <cstdint>
#include <cstring>

struct BRU;
struct ROB;
struct systemState;
constexpr unsigned LOCAL_HISTORY_BITS = 1;
constexpr unsigned LOCAL_HISTORY_MASK = (1u << LOCAL_HISTORY_BITS) - 1u;
struct DirectionContext {
  uint8_t localHistory = 0;
  bool localPrediction = false, globalPrediction = false;
};
// BP update arbitration input: both table-training sources (BRU branch results
// and CDB JAL/JALR transfers) converge to this single point so that the two
// update calls keep a fixed order (BRU candidate first) regardless of stage
// scheduling order.
struct BPUInput {
  CommitPlan commits;
  const BRU &BRUModule;
  const ROB &ROBModule;
  aluCDB cdbOut;
  SquashInfo squashDetect;
  FetchDecision fetchDecision;
  std::array<DirectionContext, FETCH_WIDTH> fetchContexts{};
  uint8_t resolvedLocalHistory = 0;
  std::array<FetchTypeInfo, FETCH_WIDTH> fetchInfo{};
  RecoveryStep recovery;
  std::array<RecoveryRasEntry, RECOVERY_WIDTH> replay{};
  BPUInput(const BRU &bru, const ROB &rob) : BRUModule(bru), ROBModule(rob) {}
};
// Tournament: resolved local history selects 3-bit local pattern counters;
// gshare global counters compete through a PC-indexed chooser (both 2-bit).
// All tables have 256 rows; local/global/chooser reset weakly NT (3/1/1).
struct DirectionPred {
  uint8_t localHistory[BHT_CAP] = {};
  uint8_t localPHT[BHT_CAP] = {};
  uint8_t globalPHT[BHT_CAP] = {};
  uint8_t selector[SELECTOR_CAP] = {};
  uint8_t GHR = 0;
  DirectionPred() {
    std::memset(localPHT, 3, sizeof(localPHT));
    std::memset(globalPHT, 1, sizeof(globalPHT));
    std::memset(selector, 1, sizeof(selector));
  }
};

// Target prediction ("where to jump"): a committed RAS baseline and its
// speculative copy. The tops are non-wrapping depths in [0, RAS_CAP].
struct TargetPred {
  BTBEntry BTB[BTB_CAP] = {};
  uint32_t specRAS[RAS_CAP] = {};
  SRAM<1, (RAS_CAP << 5), 32> archStorage;
  uint32_t archValid = 0;
  uint8_t baselineTop = 0;
  bool replayOpen = false;
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
    DirectionContext context;
    bool cond = true;
    bool isRet = false;
  };
  DirectionPred dir;
  TargetPred tgt;
  uint8_t GHRCheckpoint[CKPT_CAP] = {};
  DirectionContext predictionContext[CKPT_CAP]{};
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

  void update(uint32_t pc, bool taken, uint32_t target, uint8_t ghr,
              const DirectionContext &context, uint8_t resolvedHistory,
              systemState &CPUstate) const;
  void updateJump(uint32_t pc, uint32_t target, bool isRet, systemState &CPUstate) const;
  void shiftGHR(bool taken, systemState &CPUstate) const;
public:
  uint64_t getBranchTotal() const { return branchTotal; }
  uint64_t getBranchCorrect() const { return branchCorrect; }
  uint64_t getCondTotal() const { return condTotal; }
  uint64_t getCondCorrect() const { return condCorrect; }
  uint64_t getJalTotal() const { return jalTotal; }
  uint64_t getJalCorrect() const { return jalCorrect; }
  uint64_t getJalrTotal() const { return jalrTotal; }
  uint64_t getJalrCorrect() const { return jalrCorrect; }
  PredictInfo predict(uint32_t pc) const;
  DirectionContext contextAt(uint32_t pc) const;

  uint8_t snapshotCheckPoint() const;
  void recoverCheckPoint(const uint8_t, systemState &CPUstate) const;
  uint8_t getNextCkptId() const { return nextCkptId; }
  void tick(const BPUInput &, systemState &);
};
