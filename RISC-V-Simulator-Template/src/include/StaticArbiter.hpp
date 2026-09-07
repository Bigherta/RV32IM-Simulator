#pragma once
// StaticArbiter: the stateless arbiter family (pure combinational, zero
// flip-flops in RTL). MemArbiter / DispatchArbiter / IssueArbiter share one
// form: work() is empty, every port is a lazy Wire, and the whole always_comb
// cloud is wired once in wire_output(). The dual-CDB bus sources
// (AluCDBArbiter / LqCDBArbiter) live in CDB.hpp. The stateful FlushArbiter
// lives separately in DynamicArbiter.hpp (registered queue vs always_comb is
// the stateful/stateless hardware boundary).
#include "AGU.hpp"
#include "ALU.hpp"
#include "BRU.hpp"
#include "Decoder.hpp"
#include "LQ.hpp"
#include "PRF.hpp"
#include "ROB.hpp"
#include "RS.hpp"
#include "SQ.hpp"
#include "common.h"
#include "module.h"
#include <array>
#include <cstdint>
// Stateless memory-request arbiter (pure combinational, zero flip-flops):
// one request per cycle -- a commit-permitted store (SQ head) has priority,
// otherwise the oldest address-resolved load flagged by LQ::LoadDetect goes
// out, guarded by SQ store-address disambiguation and the squash point (the
// store branch carries no squash guard, verbatim from the inlined
// MemRequestArbiter).
struct MemArbInput {
  Wire<1> dmemBusy; // DCache busy (DCacheModule.isBusy(), busy._M_old): the
                    // arbiter never issues while the DCache has a request in
                    // flight (DCache is the arbiter's single mem client; it
                    // only accepts a decision at READY with both DMEM ports
                    // drained, so DCache idle implies DMEM idle)
  // store side (SQ head payload; addr/value gated at the wiring site so the
  // un-ready throw paths are never reached)
  Wire<1> sqEmpty;            // SQModule.isEmpty()
  Wire<7> sqHeadRobTag;       // SQModule.headRobTag()
  Wire<4> sqHead;             // SQModule.getHead() (SQ slot index)
  Wire<2> sqHeadNEnc;         // head n_bytes 2b encoding (0->1B,1->2B,2->4B)
  Wire<32> sqHeadAddr;        // gated: !isEmpty && isAddressReady(head)
  Wire<32> sqHeadValue;       // gated: !isEmpty && isValueReady(head)
  // store commit permission (ROB view of the SQ head store)
  Wire<1> robHeadEmpty;       // ROBModule.headView.isEmpty
  Wire<7> robHeadTag;         // ROBModule.headView.head
  Wire<1> robHeadCommitReady; // entry.isCommitReady[sqHeadRobTag & 0x3F]
  // load side (LQ LoadDetect hit; payload gated by loadValid -- LoadDetect
  // only returns address-ready entries)
  Wire<1> loadValid;          // LQModule.LoadDetect() != 0xFFFFFFFF
  Wire<4> loadIndex;          // hit ? LQ index : 0
  Wire<32> loadAddr;          // gated by loadValid
  Wire<7> loadRobTag;         // gated by loadValid
  Wire<1> loadIsSigned;       // gated: !LQModule.getIsUnsigned(idx)
  Wire<2> loadNEnc;           // gated, 2b encoding
  Wire<1> loadCanDispatch;    // CPU-side SQModule.canDispatchLoad(addr, tag)
  // squash guard (load branch only)
  Wire<1> squashNeed;
  Wire<7> squashTag;
};
static_assert(SQ_CAP == 16 && LQ_CAP == 16,
              "MemArbInput sqHead/loadIndex are 4-bit slot indexes");
struct MemArbOutput {
  Wire<1> valid;
  Wire<5> op;       // Operation encoding (0 when idle, verbatim default)
  Wire<32> value;
  Wire<32> address;
  Wire<1> isSigned;
  Wire<2> nEnc;     // 0->1B, 1->2B, 2->4B
  Wire<7> robTag;
  Wire<7> memIndex; // store: MEM_STORE_BIT | SQ slot; load: raw LQ index
};
struct MemArbiter : dark::Module<MemArbInput, MemArbOutput> {
  MemArbiter() { wire_output(); }
  void work() override {} // pure combinational: outputs are lazy Wires
private:
  void wire_output();
  bool storeSelected() const; // commit-permitted store wins over any load
  bool loadSelected() const;
};

// Stateless dispatch arbiter (pure combinational, zero flip-flops), written
// in RTL style: ports are the RS slot-field buses plus the PRF ready bitmap
// (prd_ready[127:0]); the arbiter indexes the bitmap with the per-slot source
// tags itself. Four independent grants (ALU/MUL/AGU/BRU), each = oldest-ready
// folded priority chain over its pool, gated by destination-isFull and the
// squash point (squash suppresses valid only -- the selected idx/tag stay
// driven, consumers gate on valid). AGU pool = LoadRS ++ StoreAddrRS
// concatenated into a single 12-entry selection tree (verbatim iteration
// order of the former software double loop). The MUL channel mirrors the ALU
// channel over the dedicated multiplyRS pool.
struct DispatchArbInput {
  Wire<1> aluFull; // ALUModule.isFull()
  Wire<1> aguFull; // AGUModule.isFull()
  Wire<1> bruFull; // BRUModule.isFull()
  Wire<1> mulFull; // MULModule.isFull()
  // RS slot-field buses (raw; src tags are Wire<7> -- the 8th sentinel bit of
  // the Register<8> is clipped at the wiring site, so a tag always indexes
  // prdReady in-bounds; stale free-slot tags are killed by busy=0)
  std::array<Wire<1>, INTEGERRS_CAP> intBusy;
  std::array<Wire<7>, INTEGERRS_CAP> intSrc1Tag, intSrc2Tag, intRobTag;
  std::array<Wire<1>, LOADRS_CAP> loadBusy;
  std::array<Wire<7>, LOADRS_CAP> loadSrc1Tag, loadSrc2Tag, loadRobTag;
  std::array<Wire<1>, STORERS_CAP> saBusy; // store-addr: single operand
  std::array<Wire<7>, STORERS_CAP> saSrc1Tag, saRobTag;
  std::array<Wire<1>, BRANCHRS_CAP> brBusy;
  std::array<Wire<7>, BRANCHRS_CAP> brSrc1Tag, brSrc2Tag, brRobTag;
  std::array<Wire<1>, MULTIPLYRS_CAP> mulBusy;
  std::array<Wire<7>, MULTIPLYRS_CAP> mulSrc1Tag, mulSrc2Tag, mulRobTag;
  std::array<Wire<1>, PRF_CAP> prdReady; // PRF ready bitmap bus
  Wire<1> squashNeed;
  Wire<7> squashTag;
};
static_assert(INTEGERRS_CAP <= 16, "rsIndex is 4-bit");
struct DispArbOutInfo {
  Wire<1> valid;
  Wire<4> rsIndex; // RS slot (0 when !valid; the -1 sentinel dies -- every
                   // consumer gates on valid)
  Wire<7> robTag;  // winner slot tag (0 when !valid, verbatim default)
  Wire<3> rsType;  // RSType encoding (Integer..StoreAddr, 5 values)
};
struct DispatchArbOutput {
  DispArbOutInfo alu, agu, bru, mul;
};
struct DispatchArbiter : dark::Module<DispatchArbInput, DispatchArbOutput> {
  DispatchArbiter() { wire_output(); }
  void work() override {} // pure combinational: outputs are lazy Wires
private:
  struct WinResult {
    bool v;
    uint32_t tag;
    uint32_t idx;
  };
  void wire_output();
  // prd_ready[tag] with P0 tied high (InvalidPhy=0 is permanently ready).
  bool readyOf(uint32_t tag) const;
  // Folded priority chain (always_comb): req[i] = busy & rdy1 & rdy2, first
  // hit occupies, strictly-older replaces (equal tag keeps the earlier slot
  // -- dead logic in RTL since busy slots hold unique tags, kept verbatim).
  template <std::size_t N>
  WinResult selectOldest(const std::array<Wire<1>, N> &busy,
                         const std::array<Wire<7>, N> &src1Tag,
                         const std::array<Wire<7>, N> &src2Tag,
                         const std::array<Wire<7>, N> &tags) const;
  WinResult aluSelect() const;
  WinResult bruSelect() const;
  WinResult mulSelect() const; // multiplyRS pool, same shape as aluSelect
  WinResult aguSelect() const; // load[0..3] ++ sa[0..7] single 12-slot pass
};
// ---- width/encoding guards for the wire-ized issue packet ----
static_assert(static_cast<uint32_t>(Operation::OP_INVALID) < 32,
              "payload op rides a 5b wire");
static_assert(static_cast<uint32_t>(ROBType::LINK) < 4,
              "robEntry.type rides a 2b wire");
static_assert(MEM_STORE_BIT == 0x40 && SQ_CAP <= 16,
              "saMemIndex packs a 4b SQ tail at bit 6 into a 7b wire");

// ---- Input: explicit port view over the seven producer modules (the
// retired IssueArbiterInput held 7 module references + a plain SquashInfo;
// the stateless-module form takes declared wires so the block maps 1:1 onto
// an always_comb cloud). Producer reads stay in the CPU wiring lambdas via
// the same bridge accessors the former build() called, so every input is
// same-source same-cycle by construction. ----
struct IssueArbInputDec {
  Wire<1> isEmpty;
  Wire<4> type;       // RISC_V enum (R..RV_INVALID, 9 values)
  Wire<7> opcode;
  Wire<3> funct3;
  Wire<7> funct7;
  Wire<5> rd, rs1, rs2;
  Wire<32> imm;
  Wire<32> pc;
  Wire<1> isHalt;
  Wire<1> allocDest;
  Wire<32> predictedPC;
  Wire<6> ckptId;
};
struct IssueArbInputRob {
  Wire<1> isFull;
  Wire<7> nextTag;
};
// Raw busy bitmaps: the module runs the verbatim first-fit tryAlloc scans
// (first-hit return kept per the comb-helper exemption).
struct IssueArbInputRs {
  std::array<Wire<1>, INTEGERRS_CAP> intBusy;
  std::array<Wire<1>, LOADRS_CAP> loadBusy;
  std::array<Wire<1>, STORERS_CAP> saBusy;
  std::array<Wire<1>, STORERS_CAP> svBusy;
  std::array<Wire<1>, BRANCHRS_CAP> brBusy;
  std::array<Wire<1>, MULTIPLYRS_CAP> mulBusy;
};
// Port = value: the free-list head slot is resolved wiring-side
// (getFreeListSlot(getHeadSeq()), a pure read) so the module needs no
// ring mechanics.
struct IssueArbInputPrf {
  Wire<1> freeListEmpty;
  Wire<7> freePhy;
};
struct IssueArbInputLsq {
  Wire<1> lqFull, sqFull;
  Wire<4> lqTail, sqTail;
  Wire<4> lqTailSnapshot, sqTailSnapshot;
};
// Raw RAT operand read; the ready->constant resolve lives in the module
// (verbatim resolveSrc, gated exactly like the former per-branch call
// sites because its assert is not superset-safe).
struct IssueArbOperandView {
  Wire<1> ready;
  Wire<7> phy;
  Wire<32> value;
};
struct IssueArbInputRat {
  IssueArbOperandView s1, s2;
  Wire<7> rdOldPhy;
};
struct IssueArbInput {
  IssueArbInputDec dec;
  IssueArbInputRob rob;
  IssueArbInputRs rs;
  IssueArbInputPrf prf;
  IssueArbInputLsq lsq;
  IssueArbInputRat rat;
  Wire<1> squashNeed;
};

// ---- Inner: shared decision nets of the always_comb cloud (zero flops;
// lazily evaluated once per cycle, cache cleared by sync). `win` encodes
// which branch's packet claims the single issue port this cycle -- every
// Output field is a small mux keyed on it:
//   0 none (guard fail / branch resource fail / unhandled opcode)
//   1 INT  2 HALT  3 LOAD  4 STORE  5 BR  6 UJ  7 RV_INVALID  8 MUL
struct IssueArbInner {
  Wire<4> win;
  // branch parameters (pure functions of dec.type/opcode)
  Wire<1> intHasRs2, intImmAsVk, intIsControl;
  Wire<1> ujHasPC, ujIsControl;
  Wire<5> opDec; // decodeOp result, shared by the four payload groups
  // first-fit free-slot scans (verbatim tryAlloc* loops)
  Wire<1> intFree;  Wire<4> intSlot;
  Wire<1> loadFree; Wire<2> loadSlot;
  Wire<1> saFree;   Wire<3> saSlot;
  Wire<1> svFree;   Wire<3> svSlot;
  Wire<1> brFree;   Wire<2> brSlot;
  Wire<1> mulFree;  Wire<2> mulSlot;
};

// ---- Output: per-consumer field groups. Field names match the retired
// IssuePacket (consumers change only their reference path); slot fields are
// 0-defaulted and has-gated (the -1 sentinel is retired with the plain
// struct -- RS gates every push by hasX, so slot garbage on non-selected
// classes was never consumed). ----
struct IssueArbOutputCore {
  Wire<1> valid;
  Wire<1> allocDest;
  Wire<7> phy;
  Wire<7> robTag;
  Wire<1> isLoad, isStore, isControl, isHalt;
  Wire<3> nBytes; // 0/1/2/4 bytes (LQ/SQ remap to 2b at their wiring)
  Wire<1> isUnsigned;
  Wire<32> pc;    // driven only for control transfers (JALR/JAL), else 0
};
struct IssueArbOutputSelect {
  Wire<1> hasInteger, hasLoad, hasStore, hasBranch, hasMultiply;
  Wire<4> integerSlot;
  Wire<2> loadSlot;
  Wire<3> storeAddrSlot, storeValueSlot;
  Wire<2> branchSlot;
  Wire<2> multiplySlot;
};
struct IssueArbOutIntP {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
};
struct IssueArbOutLoadP {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
  Wire<7> memIndex;
};
struct IssueArbOutSaP {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
  Wire<7> memIndex;
};
struct IssueArbOutSvP {
  Wire<7> dataTag;
  Wire<32> dataImm;
  Wire<8> robTag;
  Wire<7> memIndex;
};
struct IssueArbOutBrP {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
  Wire<32> imm, pc;
};
struct IssueArbOutMulP {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
};
struct IssueArbOutRobEntry {
  Wire<2> type;
  Wire<1> isCommitReady;
  Wire<5> dest;
  Wire<1> halt, isCall, isRet, isIndirect;
  Wire<6> ckptId;
  Wire<32> predictedPC;
  Wire<32> pc;
  Wire<4> lqTailSnapshot, sqTailSnapshot;
  Wire<7> newPhy, oldPhy;
};
struct IssueArbOutput {
  IssueArbOutputCore core;
  IssueArbOutputSelect select;
  IssueArbOutIntP intP;
  IssueArbOutLoadP loadP;
  IssueArbOutSaP saP;
  IssueArbOutSvP svP;
  IssueArbOutBrP brP;
  IssueArbOutMulP mulP;
  IssueArbOutRobEntry robEntry;
};

// Stateless issue arbiter (pure combinational, zero flip-flops): selects at
// most one decoded head instruction per cycle, allocates its resources (ROB
// tag, RS first-fit slot, phy reg, LSQ tail snapshots) and drives the
// per-consumer field groups. Archived in StaticArbiter.hpp with the other
// stateless arbiters; the stateful FlushArbiter lives in DynamicArbiter.hpp.
struct IssueArbiter : dark::Module<IssueArbInput, IssueArbOutput, IssueArbInner> {
  IssueArbiter() { wire_output(); }
  void work() override {}

private:
  void wire_output();
  // Raw issue class of the decoded head (pre-resource-check), mirroring the
  // former build() switch; class codes as in IssueArbInner.win.
  uint32_t issueClass() const;
  // Verbatim from the former resolveSrc: register source -> Operand.
  Operand resolveSrc(const IssueArbOperandView &v) const;
};
