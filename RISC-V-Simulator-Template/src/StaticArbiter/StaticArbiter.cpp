#include "../include/StaticArbiter.hpp"
#include <cassert>
#include <cstdint>

// ---- MemArbiter: stateless combinational memory-request arbiter ----
// Store priority + load disambiguation semantics are verbatim from the
// inlined MemRequestArbiter that used to live in CPU::comb().
bool MemArbiter::storeSelected() const {
  if (static_cast<bool>(sqEmpty) || static_cast<bool>(dmemBusy))
    return false;
  uint32_t storeTag = static_cast<uint32_t>(sqHeadRobTag);
  uint32_t head = static_cast<uint32_t>(robHeadTag);
  bool committed =
      static_cast<bool>(robHeadEmpty) || ROB::isOlder(storeTag, head);
  bool atHeadReady = !committed && storeTag == head &&
                     static_cast<bool>(robHeadCommitReady);
  return committed || atHeadReady;
}

bool MemArbiter::loadSelected() const {
  if (storeSelected() || static_cast<bool>(dmemBusy) ||
      !static_cast<bool>(loadValid) || !static_cast<bool>(loadCanDispatch))
    return false;
  return !static_cast<bool>(squashNeed) ||
         ROB::isOlder(static_cast<uint32_t>(loadRobTag),
                      static_cast<uint32_t>(squashTag));
}

void MemArbiter::wire_output() {
  valid = [this]() -> uint32_t {
    return (storeSelected() || loadSelected()) ? 1u : 0u;
  };
  op = [this]() -> uint32_t {
    if (storeSelected())
      return static_cast<uint32_t>(Operation::Store);
    if (loadSelected())
      return static_cast<uint32_t>(Operation::Load);
    return 0u;
  };
  // Load value travels through the CDB return path, not the request.
  value = [this]() -> uint32_t {
    return storeSelected() ? static_cast<uint32_t>(sqHeadValue) : 0u;
  };
  address = [this]() -> uint32_t {
    if (storeSelected())
      return static_cast<uint32_t>(sqHeadAddr);
    if (loadSelected())
      return static_cast<uint32_t>(loadAddr);
    return 0u;
  };
  // Store is never signed (verbatim memDispatchSigned = false).
  isSigned = [this]() -> uint32_t {
    return loadSelected() ? static_cast<uint32_t>(loadIsSigned) : 0u;
  };
  nEnc = [this]() -> uint32_t {
    if (storeSelected())
      return static_cast<uint32_t>(sqHeadNEnc);
    if (loadSelected())
      return static_cast<uint32_t>(loadNEnc);
    return 0u;
  };
  robTag = [this]() -> uint32_t {
    if (storeSelected())
      return static_cast<uint32_t>(sqHeadRobTag);
    if (loadSelected())
      return static_cast<uint32_t>(loadRobTag);
    return 0u;
  };
  memIndex = [this]() -> uint32_t {
    if (storeSelected())
      return static_cast<uint32_t>(MEM_STORE_BIT) |
             static_cast<uint32_t>(sqHead);
    if (loadSelected())
      return static_cast<uint32_t>(loadIndex);
    return 0u;
  };
}

// ---- DispatchArbiter: stateless combinational dispatch (RTL style) ----
// Ports are buses; the arbiter indexes the PRF ready bitmap with per-slot
// source tags itself. Selection is a folded priority chain; a grant gates on
// destination isFull and the squash point (squash suppresses valid only --
// the selected idx/tag stay driven, consumers gate on valid).
bool DispatchArbiter::readyOf(uint32_t tag) const {
  return tag == InvalidPhy ? true : static_cast<bool>(prdReady[tag]);
}

template <std::size_t N>
DispatchArbiter::WinResult DispatchArbiter::selectOldest(
    const std::array<Wire<1>, N> &busy, const std::array<Wire<7>, N> &src1Tag,
    const std::array<Wire<7>, N> &src2Tag,
    const std::array<Wire<7>, N> &tags) const {
  WinResult w{false, 0, 0};
  for (uint32_t i = 0; i < N; ++i) {
    bool req = static_cast<bool>(busy[i]) &&
               readyOf(static_cast<uint32_t>(src1Tag[i])) &&
               readyOf(static_cast<uint32_t>(src2Tag[i]));
    uint32_t tag = static_cast<uint32_t>(tags[i]);
    if (req && (!w.v || ROB::isOlder(tag, w.tag)))
      w = {true, tag, i};
  }
  return w;
}

DispatchArbiter::WinResult DispatchArbiter::aluSelect() const {
  return selectOldest(intBusy, intSrc1Tag, intSrc2Tag, intRobTag);
}

DispatchArbiter::WinResult DispatchArbiter::bruSelect() const {
  return selectOldest(brBusy, brSrc1Tag, brSrc2Tag, brRobTag);
}

DispatchArbiter::WinResult DispatchArbiter::mulSelect() const {
  return selectOldest(mulBusy, mulSrc1Tag, mulSrc2Tag, mulRobTag);
}

DispatchArbiter::WinResult DispatchArbiter::divSelect() const {
  return selectOldest(divBusy, divSrc1Tag, divSrc2Tag, divRobTag);
}

// load[0..3] ++ sa[0..7] concatenated into a single 12-slot pass -- verbatim
// iteration order and fold condition of the former software double loop
// (store-addr carries a single operand; equal tags keep the earlier slot).
DispatchArbiter::WinResult DispatchArbiter::aguSelect() const {
  WinResult w{false, 0, 0};
  for (int i = 0; i < LOADRS_CAP + STORERS_CAP; ++i) {
    bool req;
    uint32_t tag;
    if (i < LOADRS_CAP) {
      req = static_cast<bool>(loadBusy[i]) &&
            readyOf(static_cast<uint32_t>(loadSrc1Tag[i])) &&
            readyOf(static_cast<uint32_t>(loadSrc2Tag[i]));
      tag = static_cast<uint32_t>(loadRobTag[i]);
    } else {
      int j = i - LOADRS_CAP;
      req = static_cast<bool>(saBusy[j]) &&
            readyOf(static_cast<uint32_t>(saSrc1Tag[j]));
      tag = static_cast<uint32_t>(saRobTag[j]);
    }
    if (req && (!w.v || ROB::isOlder(tag, w.tag)))
      w = {true, tag, static_cast<uint32_t>(i)};
  }
  return w;
}

void DispatchArbiter::wire_output() {
  // win = ~dest_full & selected; grant = win & ~squash_kill. Fields (idx/tag/
  // type) are driven by win (not grant) -- verbatim: the former code wrote
  // them before flipping valid off under squash.
  alu.valid = [this]() -> uint32_t {
    WinResult w = aluSelect();
    return !static_cast<bool>(aluFull) && w.v &&
                   (!static_cast<bool>(squashNeed) ||
                    ROB::isOlder(w.tag, static_cast<uint32_t>(squashTag)))
               ? 1u
               : 0u;
  };
  alu.rsIndex = [this]() -> uint32_t {
    WinResult w = aluSelect();
    return !static_cast<bool>(aluFull) && w.v ? w.idx : 0u;
  };
  alu.robTag = [this]() -> uint32_t {
    WinResult w = aluSelect();
    return !static_cast<bool>(aluFull) && w.v ? w.tag : 0u;
  };
  agu.valid = [this]() -> uint32_t {
    WinResult w = aguSelect();
    return !static_cast<bool>(aguFull) && w.v &&
                   (!static_cast<bool>(squashNeed) ||
                    ROB::isOlder(w.tag, static_cast<uint32_t>(squashTag)))
               ? 1u
               : 0u;
  };
  agu.rsIndex = [this]() -> uint32_t {
    WinResult w = aguSelect();
    return !static_cast<bool>(aguFull) && w.v
               ? (w.idx < LOADRS_CAP ? w.idx : w.idx - LOADRS_CAP)
               : 0u;
  };
  agu.robTag = [this]() -> uint32_t {
    WinResult w = aguSelect();
    return !static_cast<bool>(aguFull) && w.v ? w.tag : 0u;
  };
  agu.rsType = [this]() -> uint32_t {
    WinResult w = aguSelect();
    return !static_cast<bool>(aguFull) && w.v
               ? (w.idx < LOADRS_CAP ? static_cast<uint32_t>(RSType::Load)
                                     : static_cast<uint32_t>(RSType::StoreAddr))
               : static_cast<uint32_t>(RSType::Integer);
  };
  bru.valid = [this]() -> uint32_t {
    WinResult w = bruSelect();
    return !static_cast<bool>(bruFull) && w.v &&
                   (!static_cast<bool>(squashNeed) ||
                    ROB::isOlder(w.tag, static_cast<uint32_t>(squashTag)))
               ? 1u
               : 0u;
  };
  bru.rsIndex = [this]() -> uint32_t {
    WinResult w = bruSelect();
    return !static_cast<bool>(bruFull) && w.v ? w.idx : 0u;
  };
  bru.robTag = [this]() -> uint32_t {
    WinResult w = bruSelect();
    return !static_cast<bool>(bruFull) && w.v ? w.tag : 0u;
  };
  mul.valid = [this]() -> uint32_t {
    WinResult w = mulSelect();
    return !static_cast<bool>(mulFull) && w.v &&
                   (!static_cast<bool>(squashNeed) ||
                    ROB::isOlder(w.tag, static_cast<uint32_t>(squashTag)))
               ? 1u
               : 0u;
  };
  mul.rsIndex = [this]() -> uint32_t {
    WinResult w = mulSelect();
    return !static_cast<bool>(mulFull) && w.v ? w.idx : 0u;
  };
  mul.robTag = [this]() -> uint32_t {
    WinResult w = mulSelect();
    return !static_cast<bool>(mulFull) && w.v ? w.tag : 0u;
  };
  // DIV channel: same fold as the MUL channel over the divideRS pool, but the
  // destination gate is the unit's own canAccept() instead of an isFull -- the
  // divider is a single iterative unit with no output buffer, so it must not be
  // handed new work while a finished result is still waiting to be broadcast
  // (verbatim from the main tree's div channel).
  div.valid = [this]() -> uint32_t {
    WinResult w = divSelect();
    return static_cast<bool>(divAccept) && w.v &&
                   (!static_cast<bool>(squashNeed) ||
                    ROB::isOlder(w.tag, static_cast<uint32_t>(squashTag)))
               ? 1u
               : 0u;
  };
  div.rsIndex = [this]() -> uint32_t {
    WinResult w = divSelect();
    return static_cast<bool>(divAccept) && w.v ? w.idx : 0u;
  };
  div.robTag = [this]() -> uint32_t {
    WinResult w = divSelect();
    return static_cast<bool>(divAccept) && w.v ? w.tag : 0u;
  };
}

namespace {
// Verbatim from the former IssueArbiter::decodeOp: pure multi-level switch
// over the decoded head fields (comb-helper return exemption applies).
Operation decodeOp(uint32_t type, uint32_t opcode, uint32_t funct3,
                   uint32_t funct7) {
  if (type == static_cast<uint32_t>(RISC_V::R)) {
    int link_funct = (static_cast<int>(funct3) << 7) | static_cast<int>(funct7);
    switch (link_funct) {
    case 0b0000000000:
      return Operation::ADD;
    case 0b0000100000:
      return Operation::SUB;
    case 0b0010000000:
      return Operation::SL;
    case 0b0100000000:
      return Operation::SLT;
    case 0b0110000000:
      return Operation::SLTU;
    case 0b1000000000:
      return Operation::XOR;
    case 0b1010000000:
      return Operation::SRL;
    case 0b1010100000:
      return Operation::SRA;
    case 0b1100000000:
      return Operation::OR;
    case 0b1110000000:
      return Operation::AND;
    default:
      return Operation::OP_INVALID;
    }
  }
  if (type == static_cast<uint32_t>(RISC_V::M)) {
    // funct7 == 0b0000001 (guaranteed by the decoder's M classification).
    // funct3 0..3 are the multiply ops (multiplyRS), funct3 4..7 the DIV/REM
    // family (divideRS); the divider is a real iterative SRT unit, so all
    // eight funct3 values decode (mirrors the main tree).
    switch (funct3) {
    case 0b000:
      return Operation::MUL;
    case 0b001:
      return Operation::MULH;
    case 0b010:
      return Operation::MULHSU;
    case 0b011:
      return Operation::MULHU;
    case 0b100:
      return Operation::DIV;
    case 0b101:
      return Operation::DIVU;
    case 0b110:
      return Operation::REM;
    case 0b111:
      return Operation::REMU;
    default:
      return Operation::OP_INVALID;
    }
  }
  if (type == static_cast<uint32_t>(RISC_V::I)) {
    if (opcode == 0b0000011)
      return Operation::Load;
    if (opcode == 0b1100111)
      return Operation::JALR;
    switch (funct3) {
    case 0b000:
      return Operation::ADD;
    case 0b010:
      return Operation::SLT;
    case 0b011:
      return Operation::SLTU;
    case 0b100:
      return Operation::XOR;
    case 0b110:
      return Operation::OR;
    case 0b111:
      return Operation::AND;
    default:
      return Operation::OP_INVALID;
    }
  }
  if (type == static_cast<uint32_t>(RISC_V::Istar)) {
    if (funct3 == 1)
      return Operation::SL;
    if (funct3 == 5)
      return (funct7 == 0) ? Operation::SRL : Operation::SRA;
    return Operation::OP_INVALID;
  }
  if (type == static_cast<uint32_t>(RISC_V::S)) {
    return Operation::Store;
  }
  if (type == static_cast<uint32_t>(RISC_V::B)) {
    switch (funct3) {
    case 0b000:
      return Operation::EQ;
    case 0b001:
      return Operation::NE;
    case 0b100:
      return Operation::LT;
    case 0b101:
      return Operation::GE;
    case 0b110:
      return Operation::LTU;
    case 0b111:
      return Operation::GEU;
    default:
      return Operation::OP_INVALID;
    }
  }
  if (type == static_cast<uint32_t>(RISC_V::U)) {
    if (opcode == 0b0010111)
      return Operation::AUIPC;
    if (opcode == 0b0110111)
      return Operation::LUI;
    return Operation::OP_INVALID;
  }
  if (type == static_cast<uint32_t>(RISC_V::J)) {
    return Operation::JALR;
  }
  return Operation::OP_INVALID;
}
} // namespace

// Raw issue class of the decoded head, mirroring the former build() switch
// (pre-resource-check). Note: I/U opcodes outside the handled families fall
// to none (0), exactly like the original switch's fall-through default.
uint32_t IssueArbiter::issueClass() const {
  const uint32_t type = static_cast<uint32_t>(dec.type);
  const uint32_t opcode = static_cast<uint32_t>(dec.opcode);
  if (type == static_cast<uint32_t>(RISC_V::R))
    return 1u;
  if (type == static_cast<uint32_t>(RISC_V::M)) {
    // funct3 0..3 -> multiplyRS (MUL branch, win 8); funct3 4..7 ->
    // divideRS (DIV branch, win 9). The op is re-decoded here so the
    // classification stays a pure function of the decoded head fields.
    const Operation mOp =
        decodeOp(type, opcode, static_cast<uint32_t>(dec.funct3),
                 static_cast<uint32_t>(dec.funct7));
    return (mOp == Operation::DIV || mOp == Operation::DIVU ||
            mOp == Operation::REM || mOp == Operation::REMU)
               ? 9u
               : 8u;
  }
  if (type == static_cast<uint32_t>(RISC_V::I)) {
    if (opcode == 0x13u)
      return static_cast<bool>(dec.isHalt) ? 2u : 1u;
    if (opcode == 0x67u)
      return 1u;
    if (opcode == 0x03u)
      return 3u;
    return 0u;
  }
  if (type == static_cast<uint32_t>(RISC_V::Istar))
    return 1u;
  if (type == static_cast<uint32_t>(RISC_V::S))
    return 4u;
  if (type == static_cast<uint32_t>(RISC_V::B))
    return 5u;
  if (type == static_cast<uint32_t>(RISC_V::U))
    return (opcode == 0x17u || opcode == 0x37u) ? 6u : 0u;
  if (type == static_cast<uint32_t>(RISC_V::J))
    return 6u;
  return 7u; // RV_INVALID
}

// Verbatim from the former resolveSrc: register source -> Operand. Not
// superset-safe (the P0-dead assert), so every caller gates it on its
// branch actually winning -- same shape as the former per-branch call sites.
Operand IssueArbiter::resolveSrc(const IssueArbOperandView &v) const {
  if (static_cast<bool>(v.ready)) // x0: constant zero
    return {.tag = InvalidPhy,
            .imm = static_cast<int32_t>(static_cast<uint32_t>(v.value))};
  assert(static_cast<uint32_t>(v.phy) != InvalidPhy);
  return {.tag = static_cast<int>(static_cast<uint32_t>(v.phy)), .imm = 0};
}

// ---- Single issue port: `win` picks the branch, every Output field is a
// small mux keyed on it. Failure paths reproduce the former default packet
// field by field (IssuePacket{} defaults: valid=false, phy/oldPhy/newPhy=
// InvalidPhy, tags=InvalidPhy, ops=OP_INVALID, slots/robTag/memIndex=0;
// slots now 0-default + has-gated per the retired -1 sentinel). ----
void IssueArbiter::wire_output() {
  // ---- branch parameters (pure functions of the decoded head) ----
  intImmAsVk = [this]() -> uint32_t {
    // I-0x13 (non-halt) / JALR / Istar pass imm_as_vk=true; R passes false
    if (issueClass() != 1u)
      return 0u;
    return static_cast<uint32_t>(dec.type) == static_cast<uint32_t>(RISC_V::R)
               ? 0u
               : 1u;
  };
  intIsControl = [this]() -> uint32_t {
    return (issueClass() == 1u && static_cast<uint32_t>(dec.type) ==
                                         static_cast<uint32_t>(RISC_V::I) &&
            static_cast<uint32_t>(dec.opcode) == 0x67u)
               ? 1u
               : 0u;
  };
  ujHasPC = [this]() -> uint32_t {
    // AUIPC and JAL pass has_PC=true; LUI false
    if (issueClass() != 6u)
      return 0u;
    const uint32_t type = static_cast<uint32_t>(dec.type);
    return (type == static_cast<uint32_t>(RISC_V::J) ||
            static_cast<uint32_t>(dec.opcode) == 0x17u)
               ? 1u
               : 0u;
  };
  ujIsControl = [this]() -> uint32_t {
    return (issueClass() == 6u && static_cast<uint32_t>(dec.type) ==
                                         static_cast<uint32_t>(RISC_V::J))
               ? 1u
               : 0u;
  };
  opDec = [this]() -> uint32_t {
    return static_cast<uint32_t>(decodeOp(
        static_cast<uint32_t>(dec.type), static_cast<uint32_t>(dec.opcode),
        static_cast<uint32_t>(dec.funct3), static_cast<uint32_t>(dec.funct7)));
  };

  // ---- first-fit free-slot scans over the busy bitmaps. free/slot split
  // replaces the retired -1 sentinel. ----
  intFree = [this]() -> uint32_t {
    for (int i = 0; i < INTEGERRS_CAP; i++)
      if (!static_cast<bool>(rs.intBusy[i]))
        return 1u;
    return 0u;
  };
  intSlot = [this]() -> uint32_t {
    for (int i = 0; i < INTEGERRS_CAP; i++)
      if (!static_cast<bool>(rs.intBusy[i]))
        return static_cast<uint32_t>(i);
    return 0u;
  };
  loadFree = [this]() -> uint32_t {
    for (int i = 0; i < LOADRS_CAP; i++)
      if (!static_cast<bool>(rs.loadBusy[i]))
        return 1u;
    return 0u;
  };
  loadSlot = [this]() -> uint32_t {
    for (int i = 0; i < LOADRS_CAP; i++)
      if (!static_cast<bool>(rs.loadBusy[i]))
        return static_cast<uint32_t>(i);
    return 0u;
  };
  saFree = [this]() -> uint32_t {
    for (int i = 0; i < STORERS_CAP; i++)
      if (!static_cast<bool>(rs.saBusy[i]))
        return 1u;
    return 0u;
  };
  saSlot = [this]() -> uint32_t {
    for (int i = 0; i < STORERS_CAP; i++)
      if (!static_cast<bool>(rs.saBusy[i]))
        return static_cast<uint32_t>(i);
    return 0u;
  };
  svFree = [this]() -> uint32_t {
    for (int i = 0; i < STORERS_CAP; i++)
      if (!static_cast<bool>(rs.svBusy[i]))
        return 1u;
    return 0u;
  };
  svSlot = [this]() -> uint32_t {
    for (int i = 0; i < STORERS_CAP; i++)
      if (!static_cast<bool>(rs.svBusy[i]))
        return static_cast<uint32_t>(i);
    return 0u;
  };
  brFree = [this]() -> uint32_t {
    for (int i = 0; i < BRANCHRS_CAP; i++)
      if (!static_cast<bool>(rs.brBusy[i]))
        return 1u;
    return 0u;
  };
  brSlot = [this]() -> uint32_t {
    for (int i = 0; i < BRANCHRS_CAP; i++)
      if (!static_cast<bool>(rs.brBusy[i]))
        return static_cast<uint32_t>(i);
    return 0u;
  };
  mulFree = [this]() -> uint32_t {
    for (int i = 0; i < MULTIPLYRS_CAP; i++)
      if (!static_cast<bool>(rs.mulBusy[i]))
        return 1u;
    return 0u;
  };
  mulSlot = [this]() -> uint32_t {
    for (int i = 0; i < MULTIPLYRS_CAP; i++)
      if (!static_cast<bool>(rs.mulBusy[i]))
        return static_cast<uint32_t>(i);
    return 0u;
  };
  divFree = [this]() -> uint32_t {
    for (int i = 0; i < DIVIDERS_CAP; i++)
      if (!static_cast<bool>(rs.divBusy[i]))
        return 1u;
    return 0u;
  };
  divSlot = [this]() -> uint32_t {
    for (int i = 0; i < DIVIDERS_CAP; i++)
      if (!static_cast<bool>(rs.divBusy[i]))
        return static_cast<uint32_t>(i);
    return 0u;
  };

  win = [this]() -> uint32_t {
    if (static_cast<bool>(squashNeed) || static_cast<bool>(dec.isEmpty))
      return 0u;
    const uint32_t cls = issueClass();
    const bool robFull = static_cast<bool>(rob.isFull);
    switch (cls) {
    case 1u: // INT
      return (!robFull && static_cast<bool>(intFree) &&
              (!static_cast<bool>(dec.allocDest) ||
               !static_cast<bool>(prf.freeListEmpty)))
                 ? 1u
                 : 0u;
    case 2u: // HALT
      return 2u;
    case 3u: // LOAD
      return (!robFull && !static_cast<bool>(lsq.lqFull) &&
              static_cast<bool>(loadFree) &&
              (!static_cast<bool>(dec.allocDest) ||
               !static_cast<bool>(prf.freeListEmpty)))
                 ? 3u
                 : 0u;
    case 4u: // STORE
      return (!robFull && !static_cast<bool>(lsq.sqFull) &&
              static_cast<bool>(saFree) && static_cast<bool>(svFree))
                 ? 4u
                 : 0u;
    case 5u: // BR
      return (!robFull && static_cast<bool>(brFree)) ? 5u : 0u;
    case 6u: // UJ
      return (!robFull && static_cast<bool>(intFree) &&
              (!static_cast<bool>(dec.allocDest) ||
               !static_cast<bool>(prf.freeListEmpty)))
                 ? 6u
                 : 0u;
    case 7u: // RV_INVALID
      return 7u;
    case 8u: // MUL (issue_Multiply: funct3 0..3)
      return (!robFull && static_cast<bool>(mulFree) &&
              (!static_cast<bool>(dec.allocDest) ||
               !static_cast<bool>(prf.freeListEmpty)) &&
              static_cast<uint32_t>(opDec) !=
                  static_cast<uint32_t>(Operation::OP_INVALID))
                 ? 8u
                 : 0u;
    case 9u: // DIV (issue_Divide: funct3 4..7, dedicated divideRS)
      return (!robFull && static_cast<bool>(divFree) &&
              (!static_cast<bool>(dec.allocDest) ||
               !static_cast<bool>(prf.freeListEmpty)) &&
              static_cast<uint32_t>(opDec) !=
                  static_cast<uint32_t>(Operation::OP_INVALID))
                 ? 9u
                 : 0u;
    default:
      return 0u;
    }
  };

  // ---- core fields ----
  core.valid = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) != 0u ? 1u : 0u;
  };
  core.allocDest = [this]() -> uint32_t {
    // only the renaming branches (INT / LOAD / UJ / MUL / DIV) carry the
    // allocDest block; HALT / STORE / BR / INVALID / none leave the default
    const uint32_t w = static_cast<uint32_t>(win);
    if (w != 1u && w != 3u && w != 6u && w != 8u && w != 9u)
      return 0u;
    return (static_cast<bool>(dec.allocDest) &&
            !static_cast<bool>(prf.freeListEmpty))
               ? 1u
               : 0u;
  };
  core.phy = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    if (w != 1u && w != 3u && w != 6u && w != 8u && w != 9u)
      return static_cast<uint32_t>(InvalidPhy);
    return (static_cast<bool>(dec.allocDest) &&
            !static_cast<bool>(prf.freeListEmpty))
               ? static_cast<uint32_t>(prf.freePhy)
               : static_cast<uint32_t>(InvalidPhy);
  };
  core.robTag = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    return ((w >= 1u && w <= 6u) || w == 8u || w == 9u)
               ? static_cast<uint32_t>(rob.nextTag)
               : 0u;
  };
  core.isLoad = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 3u ? 1u : 0u;
  };
  core.isStore = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u ? 1u : 0u;
  };
  core.isControl = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    return ((w == 1u && static_cast<bool>(intIsControl)) ||
            (w == 6u && static_cast<bool>(ujIsControl)))
               ? 1u
               : 0u;
  };
  core.nBytes = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    if (w != 3u && w != 4u)
      return 0u;
    const uint32_t f3 = static_cast<uint32_t>(dec.funct3);
    if (w == 3u) { // load: 0->1, 1->2, 2->4, 4->1u, 5->2u, default 4
      switch (f3) {
      case 0:
        return 1u;
      case 1:
        return 2u;
      case 2:
        return 4u;
      case 4:
        return 1u;
      case 5:
        return 2u;
      default:
        return 4u;
      }
    }
    switch (f3) { // store: 0->1, 1->2, 2->4, default 4
    case 0:
      return 1u;
    case 1:
      return 2u;
    case 2:
      return 4u;
    default:
      return 4u;
    }
  };
  core.isUnsigned = [this]() -> uint32_t {
    const uint32_t f3 = static_cast<uint32_t>(dec.funct3);
    return (static_cast<uint32_t>(win) == 3u && (f3 == 4u || f3 == 5u)) ? 1u
                                                                        : 0u;
  };
  core.pc = [this]() -> uint32_t {
    // only the control-transfer paths store inst.pc into the packet
    const uint32_t w = static_cast<uint32_t>(win);
    return ((w == 1u && static_cast<bool>(intIsControl)) ||
            (w == 6u && static_cast<bool>(ujIsControl)))
               ? static_cast<uint32_t>(dec.pc)
               : 0u;
  };

  // ---- select group (RS sel contract: has-gated, slots 0-default) ----
  // hasInteger covers BOTH integer issuers: INT (1) and UJ (6) -- the former
  // issue_UandJ also pushes into the integer RS.
  select.hasInteger = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    return (w == 1u || w == 6u) ? 1u : 0u;
  };
  select.hasLoad = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 3u ? 1u : 0u;
  };
  select.hasStore = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u ? 1u : 0u;
  };
  select.hasBranch = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 5u ? 1u : 0u;
  };
  select.hasMultiply = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 8u ? 1u : 0u;
  };
  select.integerSlot = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    return (w == 1u || w == 6u) ? static_cast<uint32_t>(intSlot) : 0u;
  };
  select.loadSlot = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 3u ? static_cast<uint32_t>(loadSlot)
                                            : 0u;
  };
  select.storeAddrSlot = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u ? static_cast<uint32_t>(saSlot)
                                            : 0u;
  };
  select.storeValueSlot = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u ? static_cast<uint32_t>(svSlot)
                                            : 0u;
  };
  select.branchSlot = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 5u ? static_cast<uint32_t>(brSlot)
                                            : 0u;
  };
  select.multiplySlot = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 8u ? static_cast<uint32_t>(mulSlot)
                                            : 0u;
  };
  select.hasDivide = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 9u ? 1u : 0u;
  };
  select.divideSlot = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 9u ? static_cast<uint32_t>(divSlot)
                                            : 0u;
  };

  // ---- intP payload (INT and UJ branches share the integer RS push) ----
  intP.op = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    return (w == 1u || w == 6u)
               ? static_cast<uint32_t>(opDec)
               : static_cast<uint32_t>(Operation::OP_INVALID);
  };
  intP.s1Tag = [this]() -> uint32_t {
    // INT: resolveSrc(rs1). UJ: has_PC writes InvPhy itself, !has_PC leaves
    // the default -- both are InvPhy, so no extra mux needed here.
    if (static_cast<uint32_t>(win) != 1u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s1).tag);
  };
  intP.s1Imm = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    if (w == 1u)
      return static_cast<uint32_t>(resolveSrc(rat.s1).imm);
    if (w == 6u) // issue_UandJ: has_PC -> pc as s1 immediate
      return static_cast<bool>(ujHasPC)
                 ? static_cast<uint32_t>(dec.pc)
                 : 0u;
    return 0u;
  };
  intP.s2Tag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 1u)
      return static_cast<uint32_t>(InvalidPhy);
    if (static_cast<bool>(intImmAsVk))
      return static_cast<uint32_t>(InvalidPhy);
    // every INT call site sets exactly one of imm_as_vk / has_rs2
    return static_cast<uint32_t>(resolveSrc(rat.s2).tag);
  };
  intP.s2Imm = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    if (w == 1u) {
      if (static_cast<bool>(intImmAsVk))
        return static_cast<uint32_t>(dec.imm);
      return static_cast<uint32_t>(resolveSrc(rat.s2).imm);
    }
    if (w == 6u) // issue_UandJ: s2 is always the immediate
      return static_cast<uint32_t>(dec.imm);
    return 0u;
  };
  intP.robTag = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    return (w == 1u || w == 6u) ? static_cast<uint32_t>(rob.nextTag) : 0u;
  };

  // ---- loadP payload (LOAD branch) ----
  loadP.op = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 3u
               ? static_cast<uint32_t>(opDec)
               : static_cast<uint32_t>(Operation::OP_INVALID);
  };
  loadP.s1Tag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 3u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s1).tag);
  };
  loadP.s1Imm = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 3u)
      return 0u;
    return static_cast<uint32_t>(resolveSrc(rat.s1).imm);
  };
  loadP.s2Tag = [this]() -> uint32_t {
    return static_cast<uint32_t>(InvalidPhy); // unconditional in issue_Load
  };
  loadP.s2Imm = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 3u ? static_cast<uint32_t>(dec.imm)
                                            : 0u;
  };
  loadP.robTag = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 3u
               ? static_cast<uint32_t>(rob.nextTag)
               : 0u;
  };
  loadP.memIndex = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 3u ? static_cast<uint32_t>(lsq.lqTail)
                                            : 0u;
  };

  // ---- saP / svP payloads (STORE branch) ----
  saP.op = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u
               ? static_cast<uint32_t>(opDec)
               : static_cast<uint32_t>(Operation::OP_INVALID);
  };
  saP.s1Tag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 4u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s1).tag);
  };
  saP.s1Imm = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 4u)
      return 0u;
    return static_cast<uint32_t>(resolveSrc(rat.s1).imm);
  };
  saP.s2Tag = [this]() -> uint32_t {
    return static_cast<uint32_t>(InvalidPhy); // unconditional in issue_Store
  };
  saP.s2Imm = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u ? static_cast<uint32_t>(dec.imm)
                                            : 0u;
  };
  saP.robTag = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u
               ? static_cast<uint32_t>(rob.nextTag)
               : 0u;
  };
  saP.memIndex = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u
               ? (static_cast<uint32_t>(lsq.sqTail) |
                  static_cast<uint32_t>(MEM_STORE_BIT))
               : 0u;
  };
  svP.dataTag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 4u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s2).tag);
  };
  svP.dataImm = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 4u)
      return 0u;
    return static_cast<uint32_t>(resolveSrc(rat.s2).imm);
  };
  svP.robTag = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u
               ? static_cast<uint32_t>(rob.nextTag)
               : 0u;
  };
  svP.memIndex = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 4u
               ? (static_cast<uint32_t>(lsq.sqTail) |
                  static_cast<uint32_t>(MEM_STORE_BIT))
               : 0u;
  };

  // ---- brP payload (BR branch) ----
  brP.op = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 5u
               ? static_cast<uint32_t>(opDec)
               : static_cast<uint32_t>(Operation::OP_INVALID);
  };
  brP.s1Tag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 5u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s1).tag);
  };
  brP.s1Imm = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 5u)
      return 0u;
    return static_cast<uint32_t>(resolveSrc(rat.s1).imm);
  };
  brP.s2Tag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 5u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s2).tag);
  };
  brP.s2Imm = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 5u)
      return 0u;
    return static_cast<uint32_t>(resolveSrc(rat.s2).imm);
  };
  brP.robTag = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 5u
               ? static_cast<uint32_t>(rob.nextTag)
               : 0u;
  };
  brP.imm = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 5u ? static_cast<uint32_t>(dec.imm)
                                            : 0u;
  };
  brP.pc = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 5u ? static_cast<uint32_t>(dec.pc)
                                            : 0u;
  };

  // ---- mulP payload (MUL branch: two register sources, dest rename) ----
  mulP.op = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 8u
               ? static_cast<uint32_t>(opDec)
               : static_cast<uint32_t>(Operation::OP_INVALID);
  };
  mulP.s1Tag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 8u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s1).tag);
  };
  mulP.s1Imm = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 8u)
      return 0u;
    return static_cast<uint32_t>(resolveSrc(rat.s1).imm);
  };
  mulP.s2Tag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 8u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s2).tag);
  };
  mulP.s2Imm = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 8u)
      return 0u;
    return static_cast<uint32_t>(resolveSrc(rat.s2).imm);
  };
  mulP.robTag = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 8u
               ? static_cast<uint32_t>(rob.nextTag)
               : 0u;
  };

  // ---- divP payload (DIV branch: identical shape to mulP) ----
  divP.op = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 9u
               ? static_cast<uint32_t>(opDec)
               : static_cast<uint32_t>(Operation::OP_INVALID);
  };
  divP.s1Tag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 9u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s1).tag);
  };
  divP.s1Imm = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 9u)
      return 0u;
    return static_cast<uint32_t>(resolveSrc(rat.s1).imm);
  };
  divP.s2Tag = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 9u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(resolveSrc(rat.s2).tag);
  };
  divP.s2Imm = [this]() -> uint32_t {
    if (static_cast<uint32_t>(win) != 9u)
      return 0u;
    return static_cast<uint32_t>(resolveSrc(rat.s2).imm);
  };
  divP.robTag = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 9u
               ? static_cast<uint32_t>(rob.nextTag)
               : 0u;
  };

  // ---- robEntry group ----
  robEntry.type = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    if (w == 1u)
      return static_cast<bool>(intIsControl)
                 ? static_cast<uint32_t>(ROBType::LINK)
                 : static_cast<uint32_t>(ROBType::REGISTER);
    if (w == 6u)
      return static_cast<bool>(ujIsControl)
                 ? static_cast<uint32_t>(ROBType::LINK)
                 : static_cast<uint32_t>(ROBType::REGISTER);
    if (w == 4u)
      return static_cast<uint32_t>(ROBType::STORE);
    if (w == 5u)
      return static_cast<uint32_t>(ROBType::BRANCH);
    return static_cast<uint32_t>(ROBType::REGISTER); // 0/2/3/7/8/9 default
  };
  robEntry.isCommitReady = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 2u ? 1u : 0u;
  };
  robEntry.dest = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    return (w == 1u || w == 2u || w == 3u || w == 6u || w == 8u || w == 9u)
               ? static_cast<uint32_t>(dec.rd)
               : 0u;
  };
  robEntry.halt = [this]() -> uint32_t {
    return static_cast<uint32_t>(win) == 2u ? 1u : 0u;
  };
  robEntry.isCall = [this]() -> uint32_t {
    // JAL with return address register
    return (static_cast<uint32_t>(win) == 6u &&
            static_cast<bool>(ujIsControl) &&
            static_cast<uint32_t>(dec.rd) == 1u)
               ? 1u
               : 0u;
  };
  robEntry.isRet = [this]() -> uint32_t {
    // JALR x0, 0(x1): return
    return (static_cast<uint32_t>(win) == 1u &&
            static_cast<bool>(intIsControl) &&
            static_cast<uint32_t>(dec.rd) == 0u &&
            static_cast<uint32_t>(dec.rs1) == 1u &&
            static_cast<uint32_t>(dec.imm) == 0u)
               ? 1u
               : 0u;
  };
  robEntry.ckptId = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    return ((w >= 1u && w <= 6u) || w == 8u || w == 9u)
               ? static_cast<uint32_t>(dec.ckptId)
               : 0u;
  };
  robEntry.predictedPC = [this]() -> uint32_t {
    // only INT / BR / UJ / MUL / DIV assign predictedPC (LOAD/STORE/HALT
    // leave 0)
    const uint32_t w = static_cast<uint32_t>(win);
    return (w == 1u || w == 5u || w == 6u || w == 8u || w == 9u)
               ? static_cast<uint32_t>(dec.predictedPC)
               : 0u;
  };
  robEntry.pc = [this]() -> uint32_t {
    const uint32_t w = static_cast<uint32_t>(win);
    return (((w >= 1u && w <= 6u) || w == 8u || w == 9u) && w != 2u)
               ? static_cast<uint32_t>(dec.pc)
               : 0u;
  };
  robEntry.lqTailSnapshot = [this]() -> uint32_t {
    // LOAD uses the include-self LQ snapshot (LoadViolation rewind guard);
    // INT/UJ/BR/MUL/DIV and STORE use the raw LQ tail.
    const uint32_t w = static_cast<uint32_t>(win);
    if (w == 1u || w == 4u || w == 5u || w == 6u || w == 8u || w == 9u)
      return static_cast<uint32_t>(lsq.lqTail);
    if (w == 3u)
      return static_cast<uint32_t>(lsq.lqTailSnapshot);
    return 0u;
  };
  robEntry.sqTailSnapshot = [this]() -> uint32_t {
    // STORE uses the include-self SQ snapshot; all other issuers the raw tail
    const uint32_t w = static_cast<uint32_t>(win);
    if (w == 1u || w == 3u || w == 5u || w == 6u || w == 8u || w == 9u)
      return static_cast<uint32_t>(lsq.sqTail);
    if (w == 4u)
      return static_cast<uint32_t>(lsq.sqTailSnapshot);
    return 0u;
  };
  robEntry.newPhy = [this]() -> uint32_t {
    // renaming branches only (INT / LOAD / UJ / MUL / DIV); STORE/BR/HALT
    // leave InvalidPhy
    const uint32_t w = static_cast<uint32_t>(win);
    if (w != 1u && w != 3u && w != 6u && w != 8u && w != 9u)
      return static_cast<uint32_t>(InvalidPhy);
    return (static_cast<bool>(dec.allocDest) &&
            !static_cast<bool>(prf.freeListEmpty))
               ? static_cast<uint32_t>(prf.freePhy)
               : static_cast<uint32_t>(InvalidPhy);
  };
  robEntry.oldPhy = [this]() -> uint32_t {
    // only INT / LOAD / UJ / MUL / DIV assign oldPhy (verbatim per-branch
    // assignments)
    const uint32_t w = static_cast<uint32_t>(win);
    if (w != 1u && w != 3u && w != 6u && w != 8u && w != 9u)
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<bool>(dec.allocDest)
               ? static_cast<uint32_t>(rat.rdOldPhy)
               : static_cast<uint32_t>(InvalidPhy);
  };
}
