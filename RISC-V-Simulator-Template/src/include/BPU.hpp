#pragma once
#include "common.h"
#include "module.h"
#include "tools.h"
#include <array>
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
constexpr int TAGE_IDX_BIT = 9;  // 512 entries per table
constexpr int TAGE_TAG_BIT = 8;
constexpr uint8_t BANKTICK_MAX = 63;
constexpr uint8_t LFSR_TAPS = 0xB8; // 8-bit Galois taps
constexpr uint8_t LFSR_SEED = 0xAC;
// The per-ckptId metadata pool must outlive every in-flight branch's
// resolve; ids are consumed one per fetch and at most ROB_CAP
// instructions can be in flight, so equal capacities guarantee no id is
// recycled before its meta is consumed.
// BP update arbitration input: both table-training sources (BRU branch results
// and CDB JAL/JALR transfers) converge to this single point so that the two
// update calls keep a fixed order (BRU candidate first) regardless of stage
// scheduling order.
struct BPUInputSquash {
  Wire<1> needSquash;
  Wire<7> SquashTag;
  Wire<6> SquashCkpt;
};
struct BPUInputCDB {
  Wire<1> cdbValid;
  Wire<32> cdbValue;
  Wire<7> cdbRobTag;
  Wire<1> cdbIsControl;
};
struct BPUInputBRU {
  Wire<1> isBRUEmpty;
  Wire<7> bruHeadRobTag;
  Wire<32> bruHeadPCResult;
  Wire<32> bruHeadPCFrom;
};
struct BPUInputROB {
  Wire<1> isROBEmpty;
  Wire<7> robHeadTag;
  std::array<Wire<32>, ROB_CAP> robPredictPC;
  std::array<Wire<32>, ROB_CAP> robPC;
  std::array<Wire<1>, ROB_CAP> robIsCall;
  std::array<Wire<1>, ROB_CAP> robIsRet;
  std::array<Wire<6>, ROB_CAP> robCkptId;
};
// Fetch-context ports feeding the prediction bundle: the fetch stage hands
// the predictor the current PC plus the fetch-stall/squash gates; the
// predictor answers through BPUOutput.fetchOut. Hardware counterpart: the
// NPC/redirect combinational cloud inside the predictor unit (fetch context
// in, prediction bundle out).
struct BPUInputFetchCtx {
  Wire<32> pc;         // FetchUnitModule.programCounter (_M_old)
  Wire<1> squashNeed;  // flushArbiter.needSquash
  Wire<1> haltFetched; // FetchUnitModule.haltFetched
  Wire<1> fqFull;      // FQModule.isFull()
  Wire<1> imemReqFull; // ICache.isRequestFull() || IMEM.isRequestFull()
};
struct BPUInputFetchTypeInfo {
  Wire<1> FetchValid;
  Wire<1> isFetchCall;
  Wire<1> isFetchRet;
  Wire<1> FetchJALTargetValid;
  Wire<32> FetchPC;
  Wire<32> FetchJALTarget;
};
struct BPUInput {
  BPUInputBRU bru;
  BPUInputCDB cdb;
  BPUInputROB rob;
  BPUInputSquash squash;
  BPUInputFetchCtx fetchCtx;
  BPUInputFetchTypeInfo fetchInfo;
};

// ---- Output: fetch-stage prediction bundle (the retired comb-built
// FetchDecision CPU member, now owned by its single producer). mid.* are
// internal combinational nets of the always_comb cloud (two <=32b packed
// nodes so predict() is evaluated at most twice per cycle; period-freeze
// makes any re-evaluation bit-identical anyway). fetchOut.* are the
// consumer-facing fields, 0-filled whenever the fetch is gated --
// bit-identical to the retired default-initialized struct.
// packed bit map (LSB-first): [0] shift, [1] shiftValue, [7:2] ckptId,
// [8] provValid, [10:9] provIdx, [13:11] provCtr, [15:14] provU,
// [16] altPred, [17] tagePred, [19:18] baseCnt.
struct BPUOutputMid {
  Wire<32> predPC; // guarded (taken ? predictPC : pc+4)
  Wire<32> packed;
};
struct BPUOutputFetch {
  Wire<1> valid;
  Wire<32> pc;
  Wire<32> predictedPC;
  Wire<1> shift;
  Wire<1> shiftValue;
  Wire<6> ckptId;
  Wire<1> provValid; // a Tn table hit supplied the prediction
  Wire<2> provIdx;   // which table (T1..T4) 0->T1, 1->T2, 2->T3, 3->T4
  Wire<8> provCtr;   // 3b provider counter, zero-extended
  Wire<2> provU;     // provider usefulness at predict time
  Wire<1> altPred;   // ALT (T0) direction
  Wire<1> tagePred;  // final tagged-predictor direction
  Wire<8> baseCnt;   // 2b T0 counter, zero-extended
};
struct BPUOutput {
  BPUOutputMid mid;
  BPUOutputFetch fetchOut;
};

// SARAS correction queue entry: the address, its LIFO position, and the
// times counter before the speculative action (so both pops and
// times inc/dec are undoable). One entry is recorded for every
// speculative call-dedup and every speculative ret.
struct AlignEntry {
  Register<32> addr;
  Register<8> index;
  Register<32> times;
};

struct RASEntry {
  Register<32> retPC;
  Register<32> times;
};
// One row of a tagged prediction table (Tn). The stored tag is a snapshot
// of (folded history ^ pc) captured at allocation time; a probe recomputes
// it from the live FoldHist views and compares. u is a 2-bit usefulness
// counter (Seznec-canonical; deliberately wider than Kunminghu's 1 bit) so
// the periodic bankTick reset halves instead of clears, letting strong
// entries survive two amnesty rounds. Allocation only targets rows with
// u == 0.
struct TageEntry {
  Register<1> valid;
  Register<8> tag; // 8b context snapshot
  Register<3> ctr; // 3b saturating direction counter
  Register<2> u;   // 2b usefulness
};
// Register-storage mirror of the plain TAGESCMeta (comb-domain) — the
// per-ckptId provider metadata captured at fetch time, restored on squash.
struct TAGE_MetaReg {
  Register<1> provValid;
  Register<2> provIdx;
  Register<3> provCtr;
  Register<2> provU;
  Register<1> altPred;
  Register<1> tagePred;
  Register<2> baseCnt;
};
// Register-storage mirror of the comb-domain BTB line.
struct BTBEntryReg {
  Register<32> actualPC;
  Register<32> target;
  Register<1> valid;
  Register<1> unconditional;
  Register<1> isCall;
  Register<1> isRet;
  Register<1> isIndirect;
};
// Register-storage mirror of the plain BPUSnapshot (comb-domain). GHR is 64b
// and Register caps at 32b, so split into two 32b halves (hi/lo).
struct BPUSnapshotReg {
  Register<32> GHR_1;
  Register<32> GHR_2;
  Register<8> alignHead;
  Register<8> alignTail;
  Register<8> RAS_top;
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
  std::array<Register<2>, T0_CAP> t0;
  std::array<Register<12>, LHT_CAP>
      LHT; // per-PC local history (12b), non-speculative
  std::array<std::array<TageEntry, 512>, TAGE_NTABLES> tn;
  std::array<Register<9>, TAGE_NTABLES> fhIdx; // W = TAGE_IDX_BIT
  std::array<Register<8>, TAGE_NTABLES> fhTag8; // W = TAGE_TAG_BIT
  std::array<Register<7>, TAGE_NTABLES>
      fhTag7; // W = TAGE_TAG_BIT - 1   （tag = 8位折 ^ 7位折）
  std::array<Register<4>, 128> useAltOnNa; // boot-reset to 0b1000
  std::array<TAGE_MetaReg, CKPT_CAP> tmeta;
  Register<32> GHR_1; // high 32b of the 64b GHR
  Register<32> GHR_2; // low 32b of the 64b GHR
  Register<6> bankTickCtr;
  Register<8> lfsr;
};

// Target prediction ("where to jump"): BTB (targets + jump type) and the
// SARAS ring return-address stack with its correction queue. All three
// ring counters are uint8_t and wrap at 256 (safe: in-flight <64, and
// ALIGNQ_CAP=32/RAS_CAP=16).
struct TargetPred {
  std::array<Register<8>, BHT_CAP> BHT;
  std::array<Register<32>, TARGETCACHE_CAP> TargetCache;
  std::array<Register<1>, TARGETCACHE_CAP> TargetValid;
  std::array<BTBEntryReg, BTB_CAP> BTB;
  std::array<RASEntry, RAS_CAP> RAS;
  Register<8> RAS_top; // ring write pointer (wraps at 256)
  std::array<AlignEntry, ALIGNQ_CAP> alignQueue;
  Register<8> alignHead; // AlignQueue head (advanced at commit)
  Register<8> alignTail; // AlignQueue tail (appended on CALL-dedup / RET)
  // Branch-type filter: set when a PC resolves as a conditional (taken or
  // not). Lets the fetch stage shift the GHR for conditionals that are not
  // BTB-resident (never-taken branches never train the BTB), so history
  // membership stops depending on BTB residency churn.
  std::array<Register<1>, CONDSEEN_CAP> condSeen;
};
struct BPUInner {
  DirectionPred dir;
  TargetPred tgt;
  std::array<BPUSnapshotReg, CKPT_CAP> bpCkpt;
  Register<6> nextCkptId;
  Register<1> bootDone; // cycle-0 init: t0=1, useAltOnNa=8
};
struct BPU : dark::Module<BPUInput, BPUOutput, BPUInner> {
  BPU() { wire_output(); }
  uint64_t branchTotal = 0;
  uint64_t branchCorrect = 0;

  // 64b GHR composed from the two 32b Register halves (_M_old view).
  uint64_t getGHR() const {
    return (static_cast<uint64_t>(static_cast<uint32_t>(dir.GHR_1)) << 32) |
           static_cast<uint32_t>(dir.GHR_2);
  }

  uint64_t getBranchTotal() const { return branchTotal; }
  uint64_t getBranchCorrect() const { return branchCorrect; }
  PredictInfo predict(int32_t pc) const;
  BPUSnapshot snapshotCheckPoint() const;
  uint8_t getNextCkptId() const { return static_cast<uint32_t>(nextCkptId); }
  void work() override;

private:
  // Fetch-direction guard, verbatim from the retired FetchDecision::build.
  bool fetchAllowed() const;
  void wire_output();
};
