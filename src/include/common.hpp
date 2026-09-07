#pragma once
#ifndef COMMON_HPP
#define COMMON_HPP
#include <cstdint>
using RobTag = uint8_t;
constexpr int INTEGERRS_CAP = 8;
constexpr int MULTIPLYRS_CAP = 4; // dedicated RS for the M-extension multiply ops
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
constexpr int AGU_CAP = 4;
constexpr int BRU_CAP = 4;
constexpr int PC_Direct_CAP = 1 << 12;
constexpr int BTB_CAP = 256;
constexpr int BHT_CAP = 1 << 8;
constexpr int T0_CAP = 1 << 10;  // local base table, (pc ^ LHT) hashed index
constexpr int LHT_CAP = 1 << 9;  // per-PC local history table, pc[11:2] index
constexpr int CONDSEEN_CAP = 1 << 9; // "this PC is a conditional" filter
constexpr uint64_t HISTORY_MASK = ~UINT64_C(0);
constexpr int LOCAL_HISTORY_BIT = 7;
constexpr int TARGETCACHE_CAP = 1 << LOCAL_HISTORY_BIT;
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
constexpr int ICACHE_BLOCK_CAP = 16;
constexpr int ICACHE_CAP =
    512; // 8KB direct-mapped (512×16B), was 1024×16B=16KB
constexpr int REQUEST_CAP = 4;
constexpr int NUM_OF_WAYS = 4;
// DCache geometry, overridable at compile time. Shrinking the cache (e.g.
// -DNUM_OF_SETS=64 -DDCACHE_INDEX_BITS=6) forces capacity evictions so the
// dirty-writeback path gets exercised; the index/tag split follows.
#ifndef NUM_OF_SETS
#define NUM_OF_SETS 1024
#endif
constexpr int DCACHE_BLOCK_CAP = 16;
#ifndef DCACHE_INDEX_BITS
#define DCACHE_INDEX_BITS 10 // log2(NUM_OF_SETS) = 1024 sets
#endif
#define DCACHE_TAG_SHIFT (4 + DCACHE_INDEX_BITS) // 16B block + set index bits
static_assert(NUM_OF_SETS == (1 << DCACHE_INDEX_BITS),
              "NUM_OF_SETS must be 2^DCACHE_INDEX_BITS");
static_assert(DCACHE_BLOCK_CAP == 16, "16B lines assumed by DCACHE_TAG_SHIFT");

enum class ValueState {
  NOTREADY,
  FETCHING,
  READY,
};

enum class SquashKind : uint8_t { None, Branch, LoadViolation };
enum class Operation {
  OP_INVALID,
  ADD,
  SUB,
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
  MUL,
  MULH,
  MULHU,
  MULHSU,
  // M-extension divide/remainder: enum reserves the full funct3 space so the
  // decode table is complete; stage A only *decodes+issues* funct3 0..3 and
  // deliberately stalls funct3 4..7 (no DIV unit yet, no silent-0 trap).
  DIV,
  DIVU,
  REM,
  REMU,
};
constexpr bool isControlOp(Operation op) { return op == Operation::JALR; }
enum class RISC_V {
  R,
  I,
  M,
  Istar,
  S,
  B,
  U,
  J,
  RV_INVALID,
};

struct AddressCalculateResult {
  int32_t value;
  uint8_t robTag;
  uint8_t memIndex;
};

struct BranchResult {
  int pcFrom;
  int pcResult;
  uint8_t robTag;
};

// Arbiter->DCache
struct MemRequest {
  Operation op; // Load | Store
  int32_t value;
  uint32_t address;
  bool isSigned;
  int n_bytes;
  uint8_t robTag;
  uint8_t memIndex;
};
struct MemDispatchDecision {
  bool valid;
  MemRequest request;
};

// DCache->DMEM
struct ReadRequest { // 线路读 / NO_ALLOCATE load 透传
  int remainCycle = 0;
  uint32_t address;
};
struct WriteRequest { // 线路写 / NO_ALLOCATE store 透传
  int remainCycle = 0;
  uint32_t address = 0;      // 块对齐(victim 基址重建)
  uint8_t lineData[16] = {}; // LINE_WRITE 载荷
};
struct DMEMRequest { // DCache → DMEM，双通道（每口一 valid）
  bool readValid = false;
  ReadRequest read{};
  bool writeValid = false;
  WriteRequest write{};
};

// DMEM->DCache
struct MemReply {
  uint8_t lineData[16] = {};
};

struct SquashInfo {
  bool needSquash = false;
  uint8_t SquashTag = 0;
  uint32_t SquashPC = 0;
  uint8_t CkptId = 0;
};
#include "CDB.hpp"

struct Operand {
  int tag = InvalidPhy;
  int32_t imm = 0;
};

// Prediction-time metadata for the tagged predictor, captured at fetch
// and consumed at branch resolution. Carried through PredictInfo /
// FetchDecision into the BPU-private per-ckptId pool.
struct TAGESCMeta {
  bool provValid = false; // a Tn table hit supplied the prediction
  uint8_t provIdx = 0;    // which table (T1..T4)
  uint8_t provCtr = 0;    // provider counter value at predict time
  uint8_t provU = 0;      // provider usefulness at predict time
  bool altPred = false;   // ALT (T0) direction
  bool tagePred = false;  // final tagged-predictor direction
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

struct BTBEntry {
  uint32_t actualPC;
  uint32_t target;
  bool valid;
  bool unconditional;
  bool isCall = false;
  bool isRet = false;
  bool isIndirect = false; // JALR: target history-dependent, TC-eligible
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

struct RATSnapshot {
  int RAT_snapshot[REGISTER_CAP];
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
struct UopView {
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
struct DispatchInfo {
  bool valid = false;
  int rsIndex = -1;
  uint8_t robTag = 0;
  RSType rsType = RSType::Integer;
};
struct DispatchBus {
  DispatchInfo alu, agu, bru, mul;
};
class ROB;
class PRF;
struct BPU;
struct FetchDecision {
  bool valid = false;
  uint32_t pc = 0;
  int32_t predictedPC = 0;
  bool shift = false;
  bool shiftValue = false;
  uint8_t ckptId = 0;
  TAGESCMeta meta{};
  static FetchDecision build(const BPU &bp, uint32_t pc,
                             const SquashInfo &squash, bool haltFetched,
                             bool fqFull, bool imemReqFull);
};
struct LoadResponse {
  bool valid = false;
  uint8_t memIndex = 0;
  uint8_t robTag = 0;
  int32_t value = 0;
};

// IMEM -> ICache line-return bus: a full 16B cache line delivered as a
// fixed-width 4x32-bit word bundle (RTL-style data bus, not a pointer).
// word index 0..3 maps to byte offsets 0..15; the critical word for a fetch
// at pc is data[(pc >> 2) & 3].
struct LineReturn {
  bool valid = false;
  uint32_t lineAddr = 0;
  uint32_t data[4] = {0, 0, 0, 0};
};
struct FetchTypeInfo {
  bool valid, isCall, isRet, jalTargetValid;
  uint32_t pc, jalTarget;
};
#endif // COMMON_HPP
