#pragma once
#include <array>
#include <cstdint>
#include "tools.h"
using RobTag = uint8_t;
constexpr int INTEGERRS_CAP = 8;
constexpr int STORERS_CAP = 4;
constexpr int LOADRS_CAP = 4;
constexpr int BRANCHRS_CAP = 4;
constexpr int LQ_CAP = 16;
constexpr int SQ_CAP = 16;
constexpr int MEMQ_SCAN_WINDOW = 8;
constexpr uint8_t MEM_STORE_BIT = 0x40;
inline bool isStoreMem(uint8_t m) { return (m & MEM_STORE_BIT) != 0; }
inline uint8_t memSlot(uint8_t m) { return m & 0x3F; }
constexpr int ROB_CAP = 64;
constexpr int FQ_CAP = 8;
constexpr int IQ_CAP = 16;
constexpr int REGISTER_CAP = 32;
constexpr int FLUSHARBITER_CAP = 4;
constexpr int ALU_CAP = 4;
constexpr int MUL_CAP = 4;
constexpr int MULTIPLYRS_CAP = 4; // dedicated RS for the M-extension multiply ops
constexpr int AGU_CAP = 4;
constexpr int BRU_CAP = 4;
constexpr int BTB_CAP = 256;
constexpr int BHT_CAP = 1 << 8;
constexpr int T0_CAP = 1 << 10;  // local base table, (pc ^ LHT) hashed index
constexpr int LHT_CAP = 1 << 9; // per-PC local history table, pc[11:2] index
constexpr int LOCAL_HISTORY_BIT = 7;
constexpr int TARGETCACHE_CAP = 1 << LOCAL_HISTORY_BIT;
constexpr int CONDSEEN_CAP = 1 << 9; // "this PC is a conditional" filter
constexpr int RAS_CAP = 8;
constexpr int ALIGNQ_CAP = 16;
constexpr int PRF_CAP = 128;
// Sentinel for "no physical register" across the whole phy-tag domain
// (RAT entries, freeList empty slots, Operand.tag immediates, ROB
// oldPhy/newPhy, IssuePacket.phy). Load-bearing invariant: P0 is never
// allocated (freeList only ever holds 32..PRF_CAP-1) and never mapped
// (RAT binds x1-x31 at reset; rd==0 never allocates), so real tags are
// always in 1..127 and 0 is unambiguous. Guarded by asserts in PRF::pop,
// PRF::push, RAT::setRAT_PRF and IssueArbiter::resolveSrc.
inline constexpr int InvalidPhy = 0;
constexpr int IMEM_CAP = 16;
constexpr int CKPT_CAP = 64;
constexpr int CACHE_BLOCK_CAP = 16;
constexpr int CACHE_CAP = 512; // 8KB direct-mapped (512x16B), mirrors main tree
constexpr int REQUEST_CAP = 4;
// ---- DCache geometry (mirrors main tree common.hpp) ----
// 64KB / 4-way / 16B lines. All constexpr: to shrink the cache for stress
// testing (capacity evictions / dirty-writeback path), edit NUM_OF_SETS and
// DCACHE_INDEX_BITS here -- they must keep the 2^DCACHE_INDEX_BITS relation.
constexpr int DCACHE_BLOCK_CAP = 16;
constexpr int NUM_OF_SETS = 1024;
constexpr int DCACHE_INDEX_BITS = 10;              // log2(NUM_OF_SETS)
constexpr int DCACHE_TAG_SHIFT = 4 + DCACHE_INDEX_BITS; // 16B block + set idx
constexpr int NUM_OF_WAYS = 4;
constexpr int MEM_LATENCY = 50;
static_assert(NUM_OF_SETS == (1 << DCACHE_INDEX_BITS),
              "NUM_OF_SETS must be 2^DCACHE_INDEX_BITS");
static_assert(DCACHE_BLOCK_CAP == 16, "16B lines assumed by DCACHE_TAG_SHIFT");
enum class ValueState : uint32_t{
  NOTREADY,
  FETCHING,
  READY,
};

enum class Operation {
  OP_INVALID,
  ADD,
  SUB,
  MUL,
  MULH,
  MULHU,
  MULHSU,
  AND,
  OR,
  XOR,
  SL,
  SRL,
  SRA,
  SLT,
  SLTU,
  AUIPC,
  LUI,
  EQ,
  GE,
  GEU,
  LT,
  LTU,
  NE,
  Load,
  Store,
  JALR,
};
constexpr bool isControlOp(Operation op) { return op == Operation::JALR; }
enum class RISC_V {
  R,
  I,
  M, // M-extension (opcode 0x33, funct7 == 1); values match the main tree
  Istar,
  S,
  B,
  U,
  J,
  RV_INVALID,
};

struct SquashInfo {
  bool needSquash = false;
  uint8_t SquashTag = 0;
  uint32_t SquashPC = 0;
  uint8_t CkptId = 0;
};

struct Operand {
  int tag = InvalidPhy;
  int32_t imm = 0;
};

// Prediction-time metadata for the tagged predictor, captured at fetch
// and consumed at branch resolution. Carried through PredictInfo into the
// BPU-private per-ckptId pool.
struct TAGESCMeta {
  bool provValid = false;  // a Tn table hit supplied the prediction
  uint8_t provIdx = 0;     // which table (T1..T4)
  uint8_t provCtr = 0;     // provider counter value at predict time
  uint8_t provU = 0;       // provider usefulness at predict time
  bool altPred = false;    // ALT (T0) direction
  bool tagePred = false;   // final tagged-predictor direction
  uint8_t baseCnt = 0;
};

struct PredictInfo {
  bool taken;
  int32_t predictPC;
  bool btbHit = false;
  bool unconditional = false;
  bool condSeen = false; // filter says this PC resolved as conditional before
  TAGESCMeta meta{};
};

struct BPUSnapshot {
  // SARAS: the checkpoint keeps GHR, AlignQueue head+tail, and RAS_top.
  // With RASEntry{retPC,times}, the height != call/ret depth, so RAS_top
  // is checkpointed directly. All three are uint8_t — ring counters wrap
  // at 256 (8× ALIGNQ_CAP / 16× RAS_CAP, safe for in-flight <64).
  // The TAGE folded views are NOT checkpointed: they are pure functions
  // of GHR, so recoverCheckPoint() refolds them from the restored
  // register instead of carrying a second copy of the truth.
  uint64_t GHR_snapshot;
  uint8_t alignHead;
  uint8_t alignTail;
  uint8_t RAS_top;
};

struct Uop {
  RISC_V type = RISC_V::RV_INVALID;
  int opcode = 0;
  int funct3 = 0;
  int funct7 = 0;
  int rd = 0;
  int rs1 = 0;
  int rs2 = 0;
  int32_t imm = 0;
  uint32_t pc = 0;
  bool isHalt = false;
  bool allocDest = false;
  int32_t predictedPC = 0;
  uint8_t ckptId = 0;
};
enum class RSType { Integer, Multiply, Branch, Load, StoreAddr };
class ROB;
class PRF;
struct BPU;

// IMEM -> ICache line-return bus: a full 16B cache line delivered as a
// fixed-width 4x32-bit word bundle (RTL-style data bus, not a pointer),
// combinational over the producer's committed state (Wire, NOT Register,
// so it adds no latency). word index 0..3 maps to byte offsets 0..15; the
// critical word for a fetch at pc is data[(pc >> 2) & 3].
struct LineReturn {
  Wire<1> valid;
  Wire<32> lineAddr;
  std::array<Wire<32>, CACHE_BLOCK_CAP / 4> data;
};

// Pre-decode scan result carried from the FQ push to the BPU. Used for RAS
// maintenance and early BTB training, independent of prediction-table hits.
struct FetchTypeInfo {
  bool valid = false;
  bool isCall = false;
  bool isRet = false;
  bool jalTargetValid = false;
  uint32_t pc = 0;
  uint32_t jalTarget = 0;
};
