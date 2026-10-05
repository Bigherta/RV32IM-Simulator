#pragma once
#include <array>
#include <bit>
#include <cstdint>

template <uint32_t N> consteval int log2Pow2() {
  static_assert(N > 0 && (N & (N - 1)) == 0,
                "log2Pow2 requires a positive power of two");
  return static_cast<int>(std::bit_width(N)) - 1;
}

using RobTag = uint8_t;
constexpr int INTEGERRS_CAP = 4;
constexpr int MULTIPLYRS_CAP = 2;
constexpr int DIVIDERS_CAP = 1;
constexpr int STORERS_CAP = 4;
constexpr int LOADRS_CAP = 4;
constexpr int BRANCHRS_CAP = 4;
constexpr int LQ_CAP = 8;
constexpr int SQ_CAP = 8;
constexpr int LQ_MASK = LQ_CAP - 1;
constexpr int SQ_MASK = SQ_CAP - 1;
constexpr int LQ_SEQ_MASK = (LQ_CAP << 1) - 1;
constexpr int SQ_SEQ_MASK = (SQ_CAP << 1) - 1;
constexpr int MEMQ_SCAN_WINDOW = SQ_CAP < 8 ? SQ_CAP : 8;
constexpr uint8_t MEM_STORE_BIT = 0x40;
inline bool isStoreMem(uint8_t m) { return (m & MEM_STORE_BIT) != 0; }
inline uint8_t memSlot(uint8_t m) { return m & 0x3F; }
constexpr uint32_t ROB_CAP = 16;
// Packed tag = {1-bit epoch, slot field}. The slot field holds 0..ROB_CAP-1,
// so its width is bit_width(ROB_CAP-1); the extra top bit is the epoch.
// ROB_CAP is not required to be a power of two: invalid slot codes between
// ROB_CAP and the field mask are never allocated (see robNextTag).
template <typename T> constexpr uint8_t ROB_TAG_BITWIDTH(T cap) {
  return std::bit_width(static_cast<uint32_t>(cap - 1)) + 1;
}
constexpr int ROB_TAG_WIDTH = ROB_TAG_BITWIDTH(ROB_CAP);
constexpr int ROB_INDEX_MASK = (1 << (ROB_TAG_WIDTH - 1)) - 1;
constexpr int ROB_TAG_MASK = (1 << ROB_TAG_WIDTH) - 1;
inline constexpr uint8_t robSlot(RobTag tag) { return tag & ROB_INDEX_MASK; }
// Successor in the packed tag space: slots advance 0..ROB_CAP-1, then the
// epoch bit flips and the slot restarts at 0. Mask-only, no divider, and for
// power-of-two ROB_CAP it degenerates to a plain (tag+1) modulo counter.
inline constexpr uint8_t robNextTag(RobTag tag) {
  return (tag & ROB_INDEX_MASK) == static_cast<int>(ROB_CAP) - 1
             ? static_cast<uint8_t>((~tag & ROB_TAG_MASK) & ~ROB_INDEX_MASK)
             : static_cast<uint8_t>((tag + 1) & ROB_TAG_MASK);
}
static_assert(ROB_CAP >= 2, "ROB needs at least two slots for age ordering");
static_assert(ROB_TAG_WIDTH <= 8,
              "RobTag is uint8_t: packed tag must fit in 8 bits");
constexpr int FQ_CAP = 4;
constexpr int IQ_CAP = 4;
constexpr int FQ_SEQ_MASK = (FQ_CAP << 1) - 1;
constexpr int IQ_SEQ_MASK = (IQ_CAP << 1) - 1;
constexpr int REGISTER_CAP = 32;
constexpr int FLUSHARBITER_CAP = 4;
constexpr int ALU_CAP = 4;
constexpr int MUL_CAP = 4;
constexpr int AGU_CAP = 4;
constexpr int BRU_CAP = 4;
constexpr int BTB_CAP = 64;
constexpr int BHT_CAP = 1 << 8;
constexpr int SELECTOR_CAP = 1 << 8;
constexpr int CONDSEEN_CAP = 1 << 9; // "this PC is a conditional" filter
// GHR lives entirely in the direction-table index domain: every consumer
// folds it in as ((PC>>2) ^ GHR) & (CAP-1), so only the low GHR_WIDTH bits
// are observable. The shift feedback and the checkpoint carrier are sized
// to this width; growing BHT_CAP/SELECTOR_CAP past 2^GHR_WIDTH requires
// growing GHR with them (guarded below).
constexpr int GHR_WIDTH = 8;
static_assert((1 << GHR_WIDTH) >= BHT_CAP &&
                  (1 << GHR_WIDTH) >= SELECTOR_CAP,
              "GHR must cover the direction-table index width");
constexpr uint16_t HISTORY_MASK = (1u << GHR_WIDTH) - 1;
constexpr int RAS_CAP = 32;
static_assert(RAS_CAP > 0 && RAS_CAP <= 0xFF,
              "RAS top is an 8-bit non-wrapping depth");
constexpr uint8_t PRF_CAP = ROB_CAP + REGISTER_CAP;
// Packed free-list sequence = {1-bit epoch, index}. Only indices
// 0..PRF_CAP-1 are allocated; codes PRF_CAP..PRF_INDEX_MASK are holes.
// For power-of-two capacities the helpers below reduce exactly to masked
// increment and subtraction. Non-power-of-two capacities skip the holes and
// reconstruct logical distance from the epoch and index fields.
template <typename T> constexpr uint8_t PRF_SEQ_BITWIDTH(T cap) {
  return std::bit_width(static_cast<uint32_t>(cap - 1)) + 1;
}
using PrfSeq = uint8_t;
constexpr uint8_t PRF_SEQ_WIDTH = PRF_SEQ_BITWIDTH(PRF_CAP);
constexpr uint8_t PRF_INDEX_WIDTH = PRF_SEQ_WIDTH - 1;
constexpr int PRF_INDEX_MASK = (1 << PRF_INDEX_WIDTH) - 1;
constexpr int PRF_SEQ_MASK = (1 << PRF_SEQ_WIDTH) - 1;

constexpr uint32_t prfSlot(PrfSeq seq) { return seq & PRF_INDEX_MASK; }

constexpr PrfSeq prfSeqNext(PrfSeq seq) {
  return prfSlot(seq) == static_cast<uint32_t>(PRF_CAP) - 1
             ? static_cast<PrfSeq>((~seq & PRF_SEQ_MASK) & ~PRF_INDEX_MASK)
             : static_cast<PrfSeq>((seq + 1) & PRF_SEQ_MASK);
}

constexpr uint32_t prfSeqDistance(PrfSeq from, PrfSeq to) {
  const int32_t indexDistance = static_cast<int32_t>(prfSlot(to)) -
                                static_cast<int32_t>(prfSlot(from));
  const int32_t fromEpoch = (from >> PRF_INDEX_WIDTH) & 1;
  const int32_t toEpoch = (to >> PRF_INDEX_WIDTH) & 1;
  int32_t distance = indexDistance;
  if (toEpoch > fromEpoch)
    distance += PRF_CAP;
  else if (toEpoch < fromEpoch)
    distance -= PRF_CAP;
  if (distance < 0)
    distance += static_cast<int32_t>(PRF_CAP) << 1;
  return static_cast<uint32_t>(distance);
}
static_assert(prfSeqNext(static_cast<PrfSeq>(PRF_CAP - 1)) ==
              static_cast<PrfSeq>(1 << PRF_INDEX_WIDTH));
static_assert(prfSeqNext(static_cast<PrfSeq>(
                  (1 << PRF_INDEX_WIDTH) | (PRF_CAP - 1))) == 0);
static_assert(prfSeqDistance(static_cast<PrfSeq>(PRF_CAP - 1),
                             static_cast<PrfSeq>(1 << PRF_INDEX_WIDTH)) == 1);
static_assert(prfSeqDistance(0,
                             static_cast<PrfSeq>(1 << PRF_INDEX_WIDTH)) ==
              PRF_CAP);
static_assert(INTEGERRS_CAP > 0 && (INTEGERRS_CAP & (INTEGERRS_CAP - 1)) == 0);
static_assert(MULTIPLYRS_CAP > 0 &&
              (MULTIPLYRS_CAP & (MULTIPLYRS_CAP - 1)) == 0);
static_assert(DIVIDERS_CAP > 0 && (DIVIDERS_CAP & (DIVIDERS_CAP - 1)) == 0);
static_assert(BRANCHRS_CAP > 0 && (BRANCHRS_CAP & (BRANCHRS_CAP - 1)) == 0);
static_assert(LQ_CAP >= 2 && LQ_CAP <= 64 && (LQ_CAP & LQ_MASK) == 0);
static_assert(SQ_CAP >= 2 && SQ_CAP <= 64 && (SQ_CAP & SQ_MASK) == 0);
static_assert(MEMQ_SCAN_WINDOW <= SQ_CAP);
static_assert(FQ_CAP >= 2 && FQ_CAP <= 128 && (FQ_CAP & (FQ_CAP - 1)) == 0);
static_assert(IQ_CAP >= 2 && IQ_CAP <= 128 && (IQ_CAP & (IQ_CAP - 1)) == 0);
static_assert(PRF_CAP > REGISTER_CAP);
static_assert(PRF_SEQ_WIDTH <= 8,
              "PrfSeq is uint8_t: packed sequence must fit in 8 bits");
static_assert(ROB_CAP < (static_cast<uint32_t>(PRF_CAP) << 1),
              "active ROB checkpoints must span less than two PRF rings");
// Sentinel for "no physical register" across the whole phy-tag domain
// (RAT entries, freeList empty slots, Operand.tag immediates, ROB
// oldPhy/newPhy, IssuePacket.phy). Load-bearing invariant: P0 is never
// allocated (freeList only ever holds 32..PRF_CAP-1) and never mapped
// (RAT binds x1-x31 at reset; rd==0 never allocates), so real tags are
// always in 1..PRF_CAP-1 and 0 is unambiguous. Guarded by asserts in PRF::pop,
// PRF::push, RAT mapping writers and IssueArbiter::resolveSrc.
inline constexpr int InvalidPhy = 0;
constexpr int IMEM_CAP = 16;
constexpr int CKPT_CAP = 32;
// RV32 word geometry, shared by instruction-line packing and SRAM lanes.
constexpr int RV32_WORD_BYTES = sizeof(uint32_t);
constexpr int RV32_WORD_BITS = RV32_WORD_BYTES << 3;
constexpr int RV32_WORD_BYTE_BITS =
    std::bit_width(static_cast<uint32_t>(RV32_WORD_BYTES - 1));

// ICache geometry: direct-mapped, so each set is one SRAM row.
constexpr int ICACHE_BLOCK_CAP = 64; // 64 B per line
constexpr int NUM_OF_ICACHE_SETS = 16;
static_assert(ICACHE_BLOCK_CAP >= RV32_WORD_BYTES &&
                  std::has_single_bit(static_cast<uint32_t>(ICACHE_BLOCK_CAP)),
              "ICache line size must be a power of two holding an RV32 word");
static_assert(NUM_OF_ICACHE_SETS >= 2 &&
                  std::has_single_bit(static_cast<uint32_t>(NUM_OF_ICACHE_SETS)),
              "ICache set count must be a power of two with at least two sets");
constexpr int ICACHE_OFFSET_BITS =
    std::bit_width(static_cast<uint32_t>(ICACHE_BLOCK_CAP - 1));
constexpr uint32_t ICACHE_OFFSET_MASK = ICACHE_BLOCK_CAP - 1u;
constexpr int ICACHE_INDEX_BITS =
    std::bit_width(static_cast<uint32_t>(NUM_OF_ICACHE_SETS - 1));
constexpr uint32_t ICACHE_INDEX_MASK = NUM_OF_ICACHE_SETS - 1u;
constexpr int ICACHE_TAG_SHIFT = ICACHE_OFFSET_BITS + ICACHE_INDEX_BITS;
constexpr int ICACHE_TAG_WIDTH = RV32_WORD_BITS - ICACHE_TAG_SHIFT;
constexpr int ICACHE_LINE_BITS = ICACHE_BLOCK_CAP << 3;
constexpr int ICACHE_WORDS_PER_LINE = ICACHE_BLOCK_CAP >> RV32_WORD_BYTE_BITS;
constexpr uint32_t ICACHE_WORD_INDEX_MASK = ICACHE_WORDS_PER_LINE - 1u;
constexpr int ICACHE_WORD_BITS = ICACHE_WORDS_PER_LINE > 1
    ? std::bit_width(static_cast<uint32_t>(ICACHE_WORDS_PER_LINE - 1)) : 1;
static_assert(ICACHE_TAG_WIDTH > 0 && ICACHE_TAG_WIDTH <= RV32_WORD_BITS,
              "ICache address split must leave a valid tag width");
constexpr int REQUEST_CAP = 4;
constexpr int CKPT_LIVE_MAX =
    ROB_CAP + REQUEST_CAP + FQ_CAP + IQ_CAP;
static_assert(CKPT_CAP > 0 && (CKPT_CAP & (CKPT_CAP - 1)) == 0,
              "checkpoint wrap uses &(CKPT_CAP-1)");
static_assert(CKPT_CAP >= CKPT_LIVE_MAX,
              "checkpoint IDs must cover ROB + ICache + FQ + IQ");
static_assert(CKPT_CAP <= (1 << 6),
              "checkpoint IDs must fit the retained 6-bit carrier");
constexpr int NUM_OF_DCACHE_WAYS = 1;
constexpr int MEM_LATENCY = 20;
// DCache geometry. The set count retains its compile-time stress override;
// all address fields and masks are derived from the basic configuration.
#ifndef NUM_OF_DCACHE_SETS
#define NUM_OF_DCACHE_SETS 128
#endif
constexpr int DCACHE_BLOCK_CAP = 64;
static_assert(DCACHE_BLOCK_CAP >= RV32_WORD_BYTES &&
                  std::has_single_bit(static_cast<uint32_t>(DCACHE_BLOCK_CAP)),
              "DCache line size must be a power of two holding an RV32 word");
static_assert(NUM_OF_DCACHE_SETS > 0 &&
                  std::has_single_bit(static_cast<uint32_t>(NUM_OF_DCACHE_SETS)),
              "DCache set count must be a positive power of two");
static_assert(NUM_OF_DCACHE_WAYS == 1 || NUM_OF_DCACHE_WAYS == 4,
              "DCache supports direct mapping or four-way tree-PLRU");
constexpr int DCACHE_OFFSET_BITS =
    std::bit_width(static_cast<uint32_t>(DCACHE_BLOCK_CAP - 1));
constexpr uint32_t DCACHE_OFFSET_MASK = DCACHE_BLOCK_CAP - 1u;
constexpr int DCACHE_INDEX_BITS =
    std::bit_width(static_cast<uint32_t>(NUM_OF_DCACHE_SETS - 1));
constexpr uint32_t DCACHE_INDEX_MASK = NUM_OF_DCACHE_SETS - 1u;
constexpr int DCACHE_TAG_SHIFT = DCACHE_OFFSET_BITS + DCACHE_INDEX_BITS;
constexpr int DCACHE_TAG_WIDTH = RV32_WORD_BITS - DCACHE_TAG_SHIFT;
constexpr int DCACHE_WAY_BITS =
    std::bit_width(static_cast<uint32_t>(NUM_OF_DCACHE_WAYS - 1));
constexpr int DCACHE_PLRU_BITS = NUM_OF_DCACHE_WAYS - 1;
static_assert(DCACHE_TAG_WIDTH > 0 && DCACHE_TAG_WIDTH <= RV32_WORD_BITS,
              "DCache address split must leave a valid tag width");

enum class ValueState {
  NOTREADY,
  FETCHING,
  READY,
};
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
  // M-extension divide/remainder operations execute in the dedicated DIV unit.
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
  uint32_t value;
  uint8_t robTag;
  uint8_t memIndex;
};

struct BranchResult {
  uint32_t pcFrom;
  uint32_t pcResult;
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
  uint8_t lineData[DCACHE_BLOCK_CAP] = {}; // LINE_WRITE 载荷
};
struct DMEMRequest { // DCache → DMEM，双通道（每口一 valid）
  bool readValid = false;
  ReadRequest read{};
  bool writeValid = false;
  WriteRequest write{};
};

// DMEM->DCache
struct MemReply {
  uint8_t lineData[DCACHE_BLOCK_CAP] = {};
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

struct PredictInfo {
  bool taken;
  uint32_t predictPC;
  bool btbHit = false;
  bool unconditional = false;
  bool condSeen = false; // filter says this PC resolved as conditional before
};

struct BTBEntry {
  uint32_t actualPC;
  uint32_t target;
  bool valid;
  bool unconditional;
  bool isRet = false;
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
enum class RSType { Integer, Multiply, Divide, Branch, Load, StoreAddr };
struct DispatchInfo {
  bool valid = false;
  int rsIndex = -1;
  uint8_t robTag = 0;
};
struct DispatchAGUInfo {
  bool valid = false;
  int rsIndex = -1;
  uint8_t robTag = 0;
  RSType rsType = RSType::Integer;
};
struct DispatchBus {
  DispatchInfo alu, bru, mul, div;
  DispatchAGUInfo agu;
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

// IMEM -> ICache line-return bus: one complete instruction line as an RV32
// word bundle (RTL-style data bus, not a pointer). The critical-word index
// uses RV32_WORD_BYTE_BITS and ICACHE_WORD_INDEX_MASK.
struct LineReturn {
  bool valid = false;
  uint32_t lineAddr = 0;
  std::array<uint32_t, ICACHE_WORDS_PER_LINE> data {};
};
struct FetchTypeInfo {
  bool valid, isCall, isRet, jalTargetValid;
  uint32_t pc, jalTarget;
};
