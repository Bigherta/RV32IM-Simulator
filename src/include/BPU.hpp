#pragma once
#include "common.hpp"
#include <cstdint>
#include <cstring>
constexpr int TAGE_NTABLES = 4;
// Power-of-two guard: the mispredict allocator indexes the table set with
// `& (TAGE_NTABLES - 1)` (hardware semantics -- no divider in the datapath).
// Changing the table count to a non-power-of-two must be a compile error here,
// not a silently synthesized modulo.
static_assert((TAGE_NTABLES & (TAGE_NTABLES - 1)) == 0,
              "TAGE_NTABLES must be a power of two (allocation uses & (N-1))");
constexpr int TAGE_HIST[TAGE_NTABLES] = {6, 12, 24, 48};
constexpr int TAGE_IDX_BIT = 7; // 128 entries per table
constexpr int TAGE_TAG_BIT = 8;
constexpr uint8_t BANKTICK_MAX = 63;
constexpr uint8_t LFSR_TAPS = 0xB8; // 8-bit Galois taps
constexpr uint8_t LFSR_SEED = 0xAC;
// The common-header CKPT_LIVE_MAX guard covers every checkpoint retained in
// ROB, ICache, FQ, or IQ before an ID can be recycled.
// The hand-unrolled folded-history update (BPU.cpp) hardcodes these H/W pairs
// as literal shift amounts and masks. If any of them move, the unroll must be
// regenerated -- make that a compile error instead of a silent mispredict.
static_assert(TAGE_NTABLES == 4, "folded-history unroll assumes 4 TAGE tables");
static_assert(TAGE_HIST[0] == 6 && TAGE_HIST[1] == 12 && TAGE_HIST[2] == 24 &&
                  TAGE_HIST[3] == 48,
              "folded-history unroll hardcodes H: must track TAGE_HIST");
static_assert(TAGE_IDX_BIT == 7 && TAGE_TAG_BIT == 8,
               "folded-history unroll hardcodes W: must track IDX/TAG bit widths");
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
  BPUInput(const BRU &bru, const ROB &rob)
      : BRUModule(bru), ROBModule(rob) {}
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
// One row of a tagged prediction table (Tn). The stored tag is a snapshot
// of (folded history ^ pc) captured at allocation time; a probe recomputes
// it from the live FoldHist views and compares. u is a 2-bit usefulness
// counter (Seznec-canonical; deliberately wider than Kunminghu's 1 bit) so
// the periodic bankTick reset halves instead of clears, letting strong
// entries survive two amnesty rounds. Allocation only targets rows with
// u == 0.
struct TageEntry {
  bool valid = false;
  uint8_t tag = 0; // 8b context snapshot
  uint8_t ctr = 0; // 3b saturating direction counter
  uint8_t u = 0;   // 2b usefulness
};

// Direction prediction ("taken or not"): the tagged tables T1..Tn keyed off
// the speculative global GHR, plus a local-history two-level base: T0 stays
// a 2b-counter table but is indexed by PC ^ (12b per-PC local history), so
// the always-available fallback tracks single-PC patterns that global
// history cannot see (interleaved streams dilute them 4-6x; an 8b/256-entry
// variant measured +1.17M cycles on pi and was rejected). Kept
// self-contained so it can be swapped wholesale for TAGE-SC later without
// touching target prediction.
struct DirectionPred {
  uint8_t t0[T0_CAP] = {};
  uint16_t LHT[LHT_CAP] = {}; // per-PC local history (12b), non-speculative
  TageEntry tn[TAGE_NTABLES][1 << TAGE_IDX_BIT];
  uint8_t useAltOnNa[128]; // ctor memset 0b1000
  TAGESCMeta tmeta[CKPT_CAP];
  uint64_t GHR = 0;
  uint8_t bankTickCtr = 0;
  uint8_t lfsr = LFSR_SEED;
  // Folded GHR views, one register per (table, W) pair instead of recomputing
  // refoldView() from GHR on every read. Synthesis-critical: the recompute form
  // is `for (s = 0; s < histLen; s += foldWidth)` -- a runtime-bounded loop
  // whose trip count depends on histLen (audit item C-6). Holding the folded
  // values in registers turns the XOR-shift forest into a fixed pipeline stage.
  // Three widths because the index fold (W = TAGE_IDX_BIT), the tag fold
  // (W = TAGE_TAG_BIT) and the tag pre-fold (W = TAGE_TAG_BIT - 1) differ.
  // Invariant: fh*[i] always equals refoldView(GHR, TAGE_HIST[i], W), so the
  // checkpoint/recover path stays GHR-only -- recoverFolds() rebuilds them.
  //
  // Unlike the template tree (fixed-width Register fields, which truncate on
  // assignment), these are plain integer fields, so EVERY update in
  // stepFolds() must mask explicitly. The widths must stay < 32 and the
  // histories must stay within the 64-bit GHR window; both are load-bearing
  // for the `disc << (H % W)` term.
  static_assert(TAGE_IDX_BIT < 32 && TAGE_TAG_BIT < 32 &&
                    TAGE_TAG_BIT - 1 < 32,
                "folded-history widths must be < 32");
  static_assert(TAGE_HIST[TAGE_NTABLES - 1] < 64,
                "HIST must fit the 64-bit GHR window");
  uint32_t fhIdx[TAGE_NTABLES] = {};   // W = TAGE_IDX_BIT
  uint8_t fhTag8[TAGE_NTABLES] = {};   // W = TAGE_TAG_BIT
  uint8_t fhTag7[TAGE_NTABLES] = {};   // W = TAGE_TAG_BIT - 1
  DirectionPred() {
    std::memset(t0, 1, sizeof(t0));
    std::memset(useAltOnNa, 8, sizeof(useAltOnNa)); // 0b1000: weakly prefer alt
  }
};

// Target prediction ("where to jump"): BTB (targets + jump type) and the
// SARAS ring return-address stack with its correction queue. All three
// ring counters are uint8_t and wrap at 256, well beyond the current
// ROB_CAP=16 and local queue capacities (ALIGNQ_CAP=16/RAS_CAP=8).
struct TargetPred {
  uint8_t BHT[BHT_CAP] = {};
  uint32_t TargetCache[TARGETCACHE_CAP] = {};
  bool TargetValid[TARGETCACHE_CAP] = {};
  BTBEntry BTB[BTB_CAP] = {};
  RASEntry RAS[RAS_CAP] = {};
  uint8_t RAS_top = 0; // ring write pointer (wraps at 256)
  AlignEntry alignQueue[ALIGNQ_CAP] = {};
  uint8_t alignHead = 0; // AlignQueue head (advanced at commit)
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
    int32_t pc = 0;
    bool taken = false;
    int32_t target = 0;
    uint64_t ghr = 0;
    bool cond = true;
    bool isCall = false;
    bool isRet = false;
    bool isIndirect = false;
    TAGESCMeta meta{};
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

  void update(int32_t pc, bool taken, int32_t target, uint64_t ghr,
              const TAGESCMeta &meta);
  void updateJump(int32_t pc, int32_t target, bool isCall, bool isRet,
                  bool isIndirect);
  void shiftGHR(bool taken);
  // Rebuild every folded view from the current GHR. Called only on squash
  // recovery (the checkpoints store GHR, so the folds are a derived view);
  // O(12) constant work, no runtime-bounded loop.
  void recoverFolds();
  // One incremental step of every folded view for a GHR left-shift of `taken`.
  // `ghrBefore` supplies the bit that is shifted out of each H-bit window.
  void stepFolds(uint64_t ghrBefore, bool taken);

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
  PredictInfo predict(int32_t pc) const;

  BPUSnapshot snapshotCheckPoint() const;
  void recoverCheckPoint(const BPUSnapshot &);
  uint8_t getNextCkptId() const { return nextCkptId; }
  void tick(const BPUInput &, systemState &);
};
