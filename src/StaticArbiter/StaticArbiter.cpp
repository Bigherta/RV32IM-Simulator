#include "../include/StaticArbiter.hpp"
#include "../include/DCache.hpp"
#include "../include/Decoder.hpp"
#include "../include/PRF.hpp"
#include "../include/RAT.hpp"
#include "../include/ROB.hpp"
#include <cassert>
#include <cstdint>

DispatchBus DispatchArbiter::arbitrate(const RSUnit &rs, const ALU &alu,
                                       const AGU &agu, const BRU &bru,
                                       const MUL &mul, const DIV &div,
                                       const PRF &prf,
                                       const SquashInfo &squash,
                                       const mulCDB &mulGrant,
                                       const divCDB &divGrant,
                                        bool divOutReady, const OperandWriteback &ports) {
  DispatchBus dispatch;
  // ALU channel: oldest operand-ready integerRS entry (I-extension ALU ops).
  // The ALU holds a single result slot that is released in the same cycle it is
  // presented (or cancelled by flush if it cannot broadcast), so the
  // buffer can take a new op every cycle and dispatch is never capacity-blocked.
  // Stated explicitly rather than leaning on the (always-true) invariant that an
  // occupied slot drains, so the gate still exists as hardware would have it.
  const bool aluCanAccept =
      !alu.isFull() ||
      (!alu.isEmpty() &&
        (!squash.needSquash ||
         ROB::isOlder(alu.headRobTag(), squash.SquashTag))) ||
      (!alu.isEmpty() && squash.needSquash &&
       !ROB::isOlder(alu.headRobTag(), squash.SquashTag));
  if (aluCanAccept) {
    bool foundAny = false;
    int best = 0;
    uint8_t bestTag = 0;
    for (int i = 0; i < INTEGERRS_CAP; ++i) {
      const auto &rs_ = rs.integerRS[i];
      if (!rs_.free && prf.isOperandReady(rs_.src1, ports) &&
          prf.isOperandReady(rs_.src2, ports)) {
        auto tag = rs_.robTag;
        if (!foundAny || ROB::isOlder(tag, bestTag)) {
          foundAny = true;
          best = i;
          bestTag = tag;
        }
      }
    }
    if (foundAny) {
      dispatch.alu.rsIndex = best;
      dispatch.alu.robTag = bestTag;
      dispatch.alu.valid = true;
      if (squash.needSquash && !ROB::isOlder(bestTag, squash.SquashTag))
        dispatch.alu.valid = false;
    }
  }
  // MUL channel: oldest operand-ready multiplyRS entry. The dedicated
  // multiply RS decouples M-ops from the integer RS shared with ALU ops,
  // so a mul-heavy stream cannot starve I-extension ALU ops of RS slots.
  if (mul.partialCanAccept(mulGrant, squash)) {
    bool foundAny = false;
    int best = 0;
    uint8_t bestTag = 0;
    for (int i = 0; i < MULTIPLYRS_CAP; ++i) {
      const auto &rs_ = rs.multiplyRS[i];
      if (!rs_.free && prf.isOperandReady(rs_.src1) &&
          prf.isOperandReady(rs_.src2)) {
        auto tag = rs_.robTag;
        if (!foundAny || ROB::isOlder(tag, bestTag)) {
          foundAny = true;
          best = i;
          bestTag = tag;
        }
      }
    }
    if (foundAny) {
      dispatch.mul.rsIndex = best;
      dispatch.mul.robTag = bestTag;
      dispatch.mul.valid = true;
      if (squash.needSquash && !ROB::isOlder(bestTag, squash.SquashTag))
        dispatch.mul.valid = false;
    }
  }
  // DIV channel: oldest operand-ready divideRS entry. The divider is a single
  // iterative context; the result can be consumed and replaced in this cycle.
  if (div.canAccept(divGrant, squash, divOutReady)) {
    bool foundAny = false;
    int best = 0;
    uint8_t bestTag = 0;
    for (int i = 0; i < DIVIDERS_CAP; ++i) {
      const auto &rs_ = rs.divideRS[i];
      if (!rs_.free && prf.isOperandReady(rs_.src1) &&
          prf.isOperandReady(rs_.src2)) {
        auto tag = rs_.robTag;
        if (!foundAny || ROB::isOlder(tag, bestTag)) {
          foundAny = true;
          best = i;
          bestTag = tag;
        }
      }
    }
    if (foundAny) {
      dispatch.div.rsIndex = best;
      dispatch.div.robTag = bestTag;
      dispatch.div.valid = true;
      if (squash.needSquash && !ROB::isOlder(bestTag, squash.SquashTag))
        dispatch.div.valid = false;
    }
  }

  // AGU channel: one memory-op dispatch per cycle, oldest first; loads
  // (both sources ready) outrank store-address (single source ready).
  const bool aguCanAccept =
      !agu.isFull() ||
      (!agu.isEmpty() &&
        (!squash.needSquash ||
         ROB::isOlder(agu.headRobTag(), squash.SquashTag))) ||
      (!agu.isEmpty() && squash.needSquash &&
       !ROB::isOlder(agu.headRobTag(), squash.SquashTag));
  if (aguCanAccept) {
    bool foundAny = false;
    int best = 0;
    uint8_t bestTag = 0;
    RSType bestType = RSType::Load;
    for (int i = 0; i < LOADRS_CAP; ++i) {
      const auto &rs_ = rs.loadRS[i];
      if (!rs_.free && prf.isOperandReady(rs_.src1, ports) &&
          prf.isOperandReady(rs_.src2, ports)) {
        auto tag = rs_.robTag;
        if (!foundAny || ROB::isOlder(tag, bestTag)) {
          foundAny = true;
          best = i;
          bestTag = tag;
          bestType = RSType::Load;
        }
      }
    }
    for (int i = 0; i < STORERS_CAP; ++i) {
      const auto &rs_ = rs.storeAddressRS[i];
      if (!rs_.free && prf.isOperandReady(rs_.src1)) {
        auto tag = rs_.robTag;
        if (!foundAny || ROB::isOlder(tag, bestTag)) {
          foundAny = true;
          best = i;
          bestTag = tag;
          bestType = RSType::StoreAddr;
        }
      }
    }
    if (foundAny) {
      dispatch.agu.rsIndex = best;
      dispatch.agu.robTag = bestTag;
      dispatch.agu.rsType = bestType;
      dispatch.agu.valid = true;
      if (squash.needSquash && !ROB::isOlder(bestTag, squash.SquashTag))
        dispatch.agu.valid = false;
    }
  }
  // BRU channel: oldest operand-ready branchRS entry.
  if (!bru.isFull()) {
    bool foundAny = false;
    int best = 0;
    uint8_t bestTag = 0;
    for (int i = 0; i < BRANCHRS_CAP; ++i) {
      const auto &rs_ = rs.branchRS[i];
      if (!rs_.free && prf.isOperandReady(rs_.src1, ports) &&
          prf.isOperandReady(rs_.src2, ports)) {
        auto tag = rs_.robTag;
        if (!foundAny || ROB::isOlder(tag, bestTag)) {
          foundAny = true;
          best = i;
          bestTag = tag;
        }
      }
    }
    if (foundAny) {
      dispatch.bru.rsIndex = best;
      dispatch.bru.robTag = bestTag;
      dispatch.bru.valid = true;
      if (squash.needSquash && !ROB::isOlder(bestTag, squash.SquashTag))
        dispatch.bru.valid = false;
    }
  }
  return dispatch;
}

MemDispatchDecision MemArbiter::arbitrate(const LQ &LQ, const SQ &SQ,
                                          const ROB &rob, const DCache &dcache,
                                        const SquashInfo &squash,
                                        const LoadAddressBypass &address) {
  MemDispatchDecision memDecision{};
  if (!dcache.isRequestFull() && !SQ.isEmpty()) {
    auto storeTag = SQ.headRobTag();
    bool committed = SQ.isCommitted(SQ.getHead());
    bool atHeadReady =
        !committed && rob.planCommit(squash, SQ).contains(storeTag);
    if (committed || atHeadReady) {
      MemRequest newRequest{};
      newRequest.address = SQ.getAddress(SQ.getHead());
      newRequest.value = SQ.getValue(SQ.getHead());
      newRequest.nBytes = SQ.getNBytes(SQ.getHead());
      newRequest.op = Operation::Store;
      newRequest.robTag = storeTag;
      newRequest.memIndex = static_cast<uint8_t>(SQ.getHead() | MEM_STORE_BIT);
      memDecision.valid = true;
      memDecision.request = newRequest;
    }
  }
  if (!memDecision.valid) {
    auto loadIndex = LQ.LoadDetect(address);
    if (loadIndex != 0xFFFFFFFF && !dcache.isRequestFull()) {
      auto loadAddr = LQ.requestAddress(loadIndex, address);
      auto loadTag = LQ.getRobTag(loadIndex);
      if (SQ.canSendLoadToCache(loadAddr, LQ.getNBytes(loadIndex), loadTag)) {
        MemRequest newRequest{};
        newRequest.address = loadAddr;
        newRequest.isSigned = !LQ.getIsUnsigned(loadIndex);
        newRequest.nBytes = LQ.getNBytes(loadIndex);
        newRequest.op = Operation::Load;
        newRequest.robTag = LQ.getRobTag(loadIndex);
        newRequest.memIndex = static_cast<uint8_t>(loadIndex);
        if (!squash.needSquash ||
            (squash.needSquash &&
             ROB::isOlder(newRequest.robTag, squash.SquashTag))) {
          memDecision.valid = true;
          memDecision.request = newRequest;
        }
      }
    }
  }
  return memDecision;
}

// Resolve an architectural register source into an Operand. RS no longer
// caches values: register sources store only the phy tag (readiness/read from
// PRF at dispatch), x0 is a constant zero, and immediates/PC are constants.
static Operand resolveSrc(const IssueArbiterInput &input, int regNum) {
  auto op = input.RATModule.readOperand(regNum);
  if (op.ready) // x0: constant zero
    return {.tag = InvalidPhy, .imm = op.value};
  assert(op.phyRegIndex !=
         InvalidPhy); // P0 is never mapped (P0-dead invariant)
  return {.tag = op.phyRegIndex, .imm = 0};
}

IssuePacket IssueArbiter::issue_IntegerRS(const IssueArbiterInput &input,
                                          const UopView &inst, bool has_rs2,
                                          bool imm_as_vk, bool isControl) {
  IssuePacket p{};
  if (input.ROBModule.isFull()) {
    return p;
  }
  int integerSlot = input.RSModule.tryAllocInteger(input.dispatchBus.alu);
  if (integerSlot < 0) {
    return p;
  }
  if (inst.allocDest && input.PRFModule.isFreeListEmpty()) {
    return p;
  }
  p.valid = true;
  p.hasInteger = true;
  p.integerSlot = integerSlot;
  p.robTag = input.ROBModule.getNextTag();
  p.integerRS.free = false;
  p.integerRS.op = decodeOp(inst);
  auto destination = inst.rd;
  p.integerRS.src1 = resolveSrc(input, inst.rs1);
  if (imm_as_vk) {
    p.integerRS.src2 = {.tag = InvalidPhy, .imm = inst.imm};
  } else if (has_rs2) {
    p.integerRS.src2 = resolveSrc(input, inst.rs2);
  }
  if (inst.allocDest && !input.PRFModule.isFreeListEmpty()) {
    p.allocDest = true;
    p.phy = input.PRFModule.getFreeListSlot(input.PRFModule.getHeadSeq());
  }
  p.robEntry = ROBEntry(ROBType::REGISTER);
  p.robEntry.dest = destination;
  p.robEntry.pc = inst.pc;
  p.robEntry.predictedPC = inst.predictedPC;
  p.robEntry.lqTailSnapshot = input.LQModule.getTail();
  p.robEntry.sqTailSnapshot = input.SQModule.getTail();
  p.robEntry.ckptId = inst.ckptId;
  p.robEntry.oldPhy =
      inst.allocDest ? input.RATModule.readRAT(destination) : InvalidPhy;
  p.robEntry.newPhy = p.phy;
  if (isControl) {
    p.isControl = true;
    p.pc = inst.pc;
    p.robEntry.type = ROBType::LINK;
    p.robEntry.isIndirect = true; // JALR path: target is register-driven
    const bool rdLink = inst.rd == 1 || inst.rd == 5;
    const bool rs1Link = inst.rs1 == 1 || inst.rs1 == 5;
    p.robEntry.isCall = rdLink;
    p.robEntry.isRet = rs1Link; // independent pop; both flags may be true
  }
  p.integerRS.robTag = p.robTag;
  return p;
}

// M-extension multiply ops (funct3 0..3) issue into the dedicated
// multiplyRS, decoupled from the integer RS so a mul-heavy stream cannot
// starve ALU-class I-ops of RS slots. The payload mirrors issue_IntegerRS
// (REGISTER ROB entry, two register sources, optional dest rename).
IssuePacket IssueArbiter::issue_Multiply(const IssueArbiterInput &input,
                                         const UopView &inst) {
  IssuePacket p{};
  if (input.ROBModule.isFull()) {
    return p;
  }
  int mulSlot = input.RSModule.tryAllocMultiply();
  if (mulSlot < 0) {
    return p;
  }
  if (inst.allocDest && input.PRFModule.isFreeListEmpty()) {
    return p;
  }
  p.valid = true;
  p.hasMultiply = true;
  p.multiplySlot = mulSlot;
  p.robTag = input.ROBModule.getNextTag();
  p.multiplyRS.free = false;
  p.multiplyRS.op = decodeOp(inst);
  auto destination = inst.rd;
  p.multiplyRS.src1 = resolveSrc(input, inst.rs1);
  p.multiplyRS.src2 = resolveSrc(input, inst.rs2);
  if (inst.allocDest && !input.PRFModule.isFreeListEmpty()) {
    p.allocDest = true;
    p.phy = input.PRFModule.getFreeListSlot(input.PRFModule.getHeadSeq());
  }
  p.robEntry = ROBEntry(ROBType::REGISTER);
  p.robEntry.dest = destination;
  p.robEntry.pc = inst.pc;
  p.robEntry.predictedPC = inst.predictedPC;
  p.robEntry.lqTailSnapshot = input.LQModule.getTail();
  p.robEntry.sqTailSnapshot = input.SQModule.getTail();
  p.robEntry.ckptId = inst.ckptId;
  p.robEntry.oldPhy =
      inst.allocDest ? input.RATModule.readRAT(destination) : InvalidPhy;
  p.robEntry.newPhy = p.phy;
  p.multiplyRS.robTag = p.robTag;
  return p;
}

// DIV/REM ops (funct3 4..7) enter the dedicated divideRS. The payload is
// identical to issue_Multiply (REGISTER ROB entry, two register sources,
// optional dest rename) -- only the RS pool and the executing unit differ.

IssuePacket IssueArbiter::issue_Divide(const IssueArbiterInput &input,
                                       const UopView &inst) {
  IssuePacket p{};
  if (input.ROBModule.isFull()) {
    return p;
  }
  int divSlot = input.RSModule.tryAllocDivide();
  if (divSlot < 0) {
    return p;
  }
  if (inst.allocDest && input.PRFModule.isFreeListEmpty()) {
    return p;
  }
  p.valid = true;
  p.hasDivide = true;
  p.divideSlot = divSlot;
  p.robTag = input.ROBModule.getNextTag();
  p.divideRS.free = false;
  p.divideRS.op = decodeOp(inst);
  auto destination = inst.rd;
  p.divideRS.src1 = resolveSrc(input, inst.rs1);
  p.divideRS.src2 = resolveSrc(input, inst.rs2);
  if (inst.allocDest && !input.PRFModule.isFreeListEmpty()) {
    p.allocDest = true;
    p.phy = input.PRFModule.getFreeListSlot(input.PRFModule.getHeadSeq());
  }
  p.robEntry = ROBEntry(ROBType::REGISTER);
  p.robEntry.dest = destination;
  p.robEntry.pc = inst.pc;
  p.robEntry.predictedPC = inst.predictedPC;
  p.robEntry.lqTailSnapshot = input.LQModule.getTail();
  p.robEntry.sqTailSnapshot = input.SQModule.getTail();
  p.robEntry.ckptId = inst.ckptId;
  p.robEntry.oldPhy =
      inst.allocDest ? input.RATModule.readRAT(destination) : InvalidPhy;
  p.robEntry.newPhy = p.phy;
  p.divideRS.robTag = p.robTag;
  return p;
}

IssuePacket IssueArbiter::issue_UandJ(const IssueArbiterInput &input,
                                      const UopView &inst, bool has_PC,
                                      bool isControl) {
  IssuePacket p{};
  if (input.ROBModule.isFull()) {
    return p;
  }
  int integerSlot = input.RSModule.tryAllocInteger(input.dispatchBus.alu);
  if (integerSlot < 0) {
    return p;
  }
  if (inst.allocDest && input.PRFModule.isFreeListEmpty()) {
    return p;
  }
  p.valid = true;
  p.hasInteger = true;
  p.integerSlot = integerSlot;
  p.robTag = input.ROBModule.getNextTag();
  p.integerRS.free = false;
  p.integerRS.op = decodeOp(inst);
  auto destination = inst.rd;
  if (has_PC) {
    p.integerRS.src1 = {.tag = InvalidPhy,
                        .imm = static_cast<int32_t>(inst.pc)};
  }
  p.integerRS.src2 = {.tag = InvalidPhy, .imm = inst.imm};
  if (inst.allocDest && !input.PRFModule.isFreeListEmpty()) {
    p.allocDest = true;
    p.phy = input.PRFModule.getFreeListSlot(input.PRFModule.getHeadSeq());
  }
  p.robEntry = ROBEntry(ROBType::REGISTER);
  p.robEntry.dest = destination;
  p.robEntry.pc = inst.pc;
  p.robEntry.predictedPC = inst.predictedPC;
  p.robEntry.lqTailSnapshot = input.LQModule.getTail();
  p.robEntry.sqTailSnapshot = input.SQModule.getTail();
  p.robEntry.ckptId = inst.ckptId;
  p.robEntry.oldPhy =
      inst.allocDest ? input.RATModule.readRAT(destination) : InvalidPhy;
  p.robEntry.newPhy = p.phy;
  if (isControl) {
    p.isControl = true;
    p.pc = inst.pc;
    p.robEntry.type = ROBType::LINK;
    p.robEntry.isCall = inst.rd == 1 || inst.rd == 5;
  }
  p.integerRS.robTag = p.robTag;
  return p;
}

IssuePacket IssueArbiter::issue_B(const IssueArbiterInput &input,
                                  const UopView &inst) {
  IssuePacket p{};
  if (input.ROBModule.isFull()) {
    return p;
  }
  int branchSlot = input.RSModule.tryAllocBranch(input.dispatchBus.bru);
  if (branchSlot < 0) {
    return p;
  }
  p.valid = true;
  p.hasBranch = true;
  p.branchSlot = branchSlot;
  p.robTag = input.ROBModule.getNextTag();
  p.branchRS.free = false;
  p.branchRS.op = decodeOp(inst);
  p.branchRS.imm = inst.imm;
  p.branchRS.pc = inst.pc;
  p.branchRS.src1 = resolveSrc(input, inst.rs1);
  p.branchRS.src2 = resolveSrc(input, inst.rs2);
  p.robEntry = ROBEntry(ROBType::BRANCH);
  p.robEntry.pc = inst.pc;
  p.robEntry.predictedPC = inst.predictedPC;
  p.robEntry.lqTailSnapshot = input.LQModule.getTail();
  p.robEntry.sqTailSnapshot = input.SQModule.getTail();
  p.robEntry.ckptId = inst.ckptId;
  p.branchRS.robTag = p.robTag;
  return p;
}

IssuePacket IssueArbiter::issue_Load(const IssueArbiterInput &input,
                                     const UopView &inst, int nBytes,
                                     bool isUnsigned) {
  IssuePacket p{};
  if (input.ROBModule.isFull() || input.LQModule.isFull()) {
    return p;
  }
  const auto &release = input.dispatchBus.agu;
  int loadSlot = input.RSModule.tryAllocLoad();
  if (loadSlot < 0 && release.valid && release.rsType == RSType::Load)
    loadSlot = release.rsIndex;
  if (loadSlot < 0) {
    return p;
  }
  if (inst.allocDest && input.PRFModule.isFreeListEmpty()) {
    return p;
  }
  p.valid = true;
  p.hasLoad = true;
  p.isLoad = true;
  p.loadSlot = loadSlot;
  p.nBytes = nBytes;
  p.isUnsigned = isUnsigned;
  p.robTag = input.ROBModule.getNextTag();
  p.loadRS.memIndex = input.LQModule.getTail() & LQ_MASK;
  p.loadRS.free = false;
  p.loadRS.op = decodeOp(inst);
  auto destination = inst.rd;

  p.loadRS.src1 = resolveSrc(input, inst.rs1);
  p.loadRS.src2 = {.tag = InvalidPhy, .imm = inst.imm};
  if (inst.allocDest && !input.PRFModule.isFreeListEmpty()) {
    p.allocDest = true;
    p.phy = input.PRFModule.getFreeListSlot(input.PRFModule.getHeadSeq());
  }
  p.robEntry = ROBEntry(ROBType::REGISTER);
  p.robEntry.dest = destination;
  p.robEntry.pc = inst.pc;
  // Keep the include-self LQ boundary for the load's ROB recovery snapshot.
  p.robEntry.lqTailSnapshot = input.LQModule.getTailSnapshot();
  p.robEntry.sqTailSnapshot = input.SQModule.getTail();
  p.robEntry.oldPhy =
      inst.allocDest ? input.RATModule.readRAT(destination) : InvalidPhy;
  p.robEntry.newPhy = p.phy;
  p.robEntry.ckptId = inst.ckptId;
  p.loadRS.robTag = p.robTag;
  return p;
}

IssuePacket IssueArbiter::issue_Store(const IssueArbiterInput &input,
                                      const UopView &inst, int nBytes) {
  IssuePacket p{};
  if (input.ROBModule.isFull() || input.SQModule.isFull()) {
    return p;
  }
  int storeAddrSlot = input.RSModule.tryAllocStoreAddress();
  int storeValueSlot = input.RSModule.tryAllocStoreValue();
  if (storeAddrSlot < 0 || storeValueSlot < 0) {
    return p;
  }
  p.valid = true;
  p.hasStore = true;
  p.isStore = true;
  p.storeAddrSlot = storeAddrSlot;
  p.storeValueSlot = storeValueSlot;
  p.nBytes = nBytes;
  p.robTag = input.ROBModule.getNextTag();
  p.storeAddrRS.memIndex =
      static_cast<uint8_t>((input.SQModule.getTail() & SQ_MASK) | MEM_STORE_BIT);
  p.storeValueRS.memIndex = p.storeAddrRS.memIndex;
  p.storeAddrRS.free = false;
  p.storeAddrRS.op = decodeOp(inst);

  p.storeAddrRS.src1 = resolveSrc(input, inst.rs1);
  p.storeAddrRS.src2 = {.tag = InvalidPhy, .imm = inst.imm};

  p.storeValueRS.free = false;
  p.storeValueRS.data = resolveSrc(input, inst.rs2);
  p.robEntry = ROBEntry(ROBType::STORE);
  p.robEntry.pc = inst.pc;
  // Include-self boundary on the SQ side; the store never enters the LQ,
  // so its lqTailSnapshot stays the raw tail.
  p.robEntry.lqTailSnapshot = input.LQModule.getTail();
  p.robEntry.sqTailSnapshot = input.SQModule.getTailSnapshot();
  p.robEntry.ckptId = inst.ckptId;
  p.storeAddrRS.robTag = p.robTag;
  p.storeValueRS.robTag = p.robTag;
  return p;
}

Operation IssueArbiter::decodeOp(const UopView &inst) {
  if (inst.type == RISC_V::R) {
    int link_funct = (inst.funct3 << 7) | inst.funct7;
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
  if (inst.type == RISC_V::M) {
    // funct7 == 0b0000001 (guaranteed by the decoder's M classification).
    // funct3 0..3 are the multiply ops, funct3 4..7 the DIV/REM family. The
    // divider is a real iterative SRT unit now, so all eight are decodable.
    switch (inst.funct3) {
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
  if (inst.type == RISC_V::I) {
    if (inst.opcode == 0b0000011)
      return Operation::Load;
    if (inst.opcode == 0b1100111)
      return Operation::JALR;
    switch (inst.funct3) {
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
  if (inst.type == RISC_V::Istar) {
    if (inst.funct3 == 1)
      return Operation::SL;
    if (inst.funct3 == 5)
      return (inst.funct7 == 0) ? Operation::SRL : Operation::SRA;
    return Operation::OP_INVALID;
  }
  if (inst.type == RISC_V::S) {
    return Operation::Store;
  }
  if (inst.type == RISC_V::B) {
    switch (inst.funct3) {
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
  if (inst.type == RISC_V::U) {
    if (inst.opcode == 0b0010111)
      return Operation::AUIPC;
    if (inst.opcode == 0b0110111)
      return Operation::LUI;
    return Operation::OP_INVALID;
  }
  if (inst.type == RISC_V::J) {
    return Operation::JALR;
  }
  return Operation::OP_INVALID;
}

IssuePacket IssueArbiter::build(const IssueArbiterInput &input) {
  IssuePacket issuePacket{};
  if (input.squashDetect.needSquash || input.ROBModule.isTrapCommitted() || input.DecodeUnitModule.isEmpty())
    return issuePacket;
  UopView inst;
  inst.type = input.DecodeUnitModule.headType();
  inst.opcode = input.DecodeUnitModule.headOpcode();
  inst.funct3 = input.DecodeUnitModule.headFunct3();
  inst.funct7 = input.DecodeUnitModule.headFunct7();
  inst.rd = input.DecodeUnitModule.headRd();
  inst.rs1 = input.DecodeUnitModule.headRs1();
  inst.rs2 = input.DecodeUnitModule.headRs2();
  inst.imm = input.DecodeUnitModule.headImm();
  inst.pc = input.DecodeUnitModule.headPc();
  inst.isHalt = input.DecodeUnitModule.headIsHalt();
  inst.allocDest = input.DecodeUnitModule.headAllocDest();
  inst.predictedPC = input.DecodeUnitModule.headPredictedPC();
  inst.ckptId = input.DecodeUnitModule.headCkptId();
  switch (inst.type) {
  case RISC_V::R: {
    issuePacket = issue_IntegerRS(input, inst, true, false, false);
    break;
  }
  case RISC_V::M: {
    // funct3 0..3 -> dedicated multiplyRS, funct3 4..7 -> dedicated divideRS.
    // Both pools are decoupled from integerRS so an M-heavy stream cannot
    // starve ALU-class I-ops of RS slots.
    auto mOp = decodeOp(inst);
    if (mOp == Operation::OP_INVALID)
      break;
    if (mOp == Operation::DIV || mOp == Operation::DIVU ||
        mOp == Operation::REM || mOp == Operation::REMU) {
      issuePacket = issue_Divide(input, inst);
    } else {
      issuePacket = issue_Multiply(input, inst);
    }
    break;
  }
  case RISC_V::I: {
    if (inst.opcode == 0b0010011) {
      if (inst.isHalt) {
        // HALT still consumes a ROB slot: issuing into a full ROB would
        // overwrite the head entry (robSlot of next equals robSlot of head).
        if (input.ROBModule.isFull())
          return issuePacket;
        issuePacket.valid = true;
        issuePacket.isHalt = true;
        issuePacket.robTag = input.ROBModule.getNextTag();
        issuePacket.robEntry = ROBEntry(ROBType::REGISTER);
        issuePacket.robEntry.dest = inst.rd;
        issuePacket.robEntry.pc = inst.pc;
        issuePacket.robEntry.predictedPC = inst.predictedPC;
        issuePacket.robEntry.ckptId = inst.ckptId;
        issuePacket.robEntry.halt = true;
        issuePacket.robEntry.isCommitReady = true;
      } else {
        issuePacket = issue_IntegerRS(input, inst, false, true, false);
      }
    } else if (inst.opcode == 0b1100111) {
      issuePacket = issue_IntegerRS(input, inst, false, true, true);
    } else if (inst.opcode == 0b0000011) {
      int nBytes = 4;
      bool isUnsigned = false;
      switch (inst.funct3) {
      case 0b000:
        nBytes = 1;
        isUnsigned = false;
        break;
      case 0b001:
        nBytes = 2;
        isUnsigned = false;
        break;
      case 0b010:
        nBytes = 4;
        isUnsigned = false;
        break;
      case 0b100:
        nBytes = 1;
        isUnsigned = true;
        break;
      case 0b101:
        nBytes = 2;
        isUnsigned = true;
        break;
      default:
        break;
      }
      issuePacket = issue_Load(input, inst, nBytes, isUnsigned);
    }
    break;
  }
  case RISC_V::Istar: {
    issuePacket = issue_IntegerRS(input, inst, false, true, false);
    break;
  }
  case RISC_V::S: {
    int nBytes = 4;
    switch (inst.funct3) {
    case 0b000:
      nBytes = 1;
      break;
    case 0b001:
      nBytes = 2;
      break;
    case 0b010:
      nBytes = 4;
      break;
    default:
      break;
    }
    issuePacket = issue_Store(input, inst, nBytes);
    break;
  }
  case RISC_V::B: {
    issuePacket = issue_B(input, inst);
    break;
  }
  case RISC_V::U: {
    if (inst.opcode == 0b0010111) {
      issuePacket = issue_UandJ(input, inst, true, false);
    } else if (inst.opcode == 0b0110111) {
      issuePacket = issue_UandJ(input, inst, false, false);
    }
    break;
  }
  case RISC_V::J: {
    issuePacket = issue_UandJ(input, inst, true, true);
    break;
  }
  case RISC_V::RV_INVALID: {
    // The invalid row still consumes a ROB slot: never push when full.
    if (!input.ROBModule.isFull()) {
      issuePacket.valid = true;
      issuePacket.robTag = input.ROBModule.getNextTag();
      issuePacket.robEntry.pc = inst.pc;
      issuePacket.robEntry.predictedPC = inst.predictedPC;
      issuePacket.robEntry.ckptId = inst.ckptId;
      issuePacket.robEntry.isCommitReady = true;
      issuePacket.robEntry.exception = true;
    }
    break;
  }
  }
  return issuePacket;
}

IssueGroup IssueArbiter::buildPrefix(const IssueArbiterInput &input) {
  IssueGroup group{};
  if constexpr (ISSUE_WIDTH == 1) {
    group.lanes[0] = build(input);
    group.count = unsigned(group.lanes[0].valid);
  } else {
    // Only small resource masks/cursors and accepted event payloads are folded.
    // No RAT/ROB/PRF/LSQ next-state image or retirement-credit bypass.
    std::array<uint32_t, 7> free{}, reuse{};
    const auto &rs = input.RSModule;
    for (int row = 0; row < INTEGERRS_CAP; ++row) if (rs.integerRS[row].free) free[0] |= 1u << row;
    for (int row = 0; row < MULTIPLYRS_CAP; ++row) if (rs.multiplyRS[row].free) free[1] |= 1u << row;
    for (int row = 0; row < DIVIDERS_CAP; ++row) if (rs.divideRS[row].free) free[2] |= 1u << row;
    for (int row = 0; row < BRANCHRS_CAP; ++row) if (rs.branchRS[row].free) free[3] |= 1u << row;
    for (int row = 0; row < LOADRS_CAP; ++row) if (rs.loadRS[row].free) free[4] |= 1u << row;
    for (int row = 0; row < STORERS_CAP; ++row) {
      if (rs.storeAddressRS[row].free) free[5] |= 1u << row;
      if (rs.storeValueRS[row].free) free[6] |= 1u << row;
    }
    const auto &dispatch = input.dispatchBus;
    if (dispatch.alu.valid) reuse[0] = 1u << dispatch.alu.rsIndex;
    if (dispatch.bru.valid) reuse[3] = 1u << dispatch.bru.rsIndex;
    if (dispatch.agu.valid && dispatch.agu.rsType == RSType::Load) reuse[4] = 1u << dispatch.agu.rsIndex;
    if constexpr (ISSUE_WIDTH == 4)
      for (unsigned pool = 0; pool < 7; ++pool) free[pool] |= reuse[pool];
    unsigned loads = 0, stores = 0, physical = 0;
    const unsigned robUsed = (input.ROBModule.getNextTag() - input.ROBModule.getHead()) & ROB_TAG_MASK;
    PrfSeq phyHead = input.PRFModule.getHeadSeq();
    const unsigned phyCredits = input.PRFModule.freeCount();
    const unsigned loadCredits = LQ_CAP - input.LQModule.count();
    const unsigned storeCredits = SQ_CAP - input.SQModule.count();
    RobTag tag = input.ROBModule.getNextTag();
    bool open = !input.squashDetect.needSquash && !input.ROBModule.isTrapCommitted();
    for (unsigned lane = 0; lane < ISSUE_WIDTH; ++lane) {
      if (open && lane < input.DecodeUnitModule.count()) {
        const auto &u = input.DecodeUnitModule.entryAt(lane);
        UopView inst{u.type, u.opcode, u.funct3, u.funct7, u.rd, u.rs1, u.rs2,
                     u.imm, u.pc, u.isHalt, u.allocDest, u.predictedPC, u.ckptId};
        const Operation op = decodeOp(inst);
        const bool halt = u.isHalt;
        const bool invalid = u.type == RISC_V::RV_INVALID;
        const bool load = u.type == RISC_V::I && u.opcode == 3;
        const bool store = u.type == RISC_V::S;
        const bool branch = u.type == RISC_V::B;
        const bool upper = u.type == RISC_V::U || u.type == RISC_V::J;
        const bool multiply = u.type == RISC_V::M && u.funct3 < 4;
        const bool divide = u.type == RISC_V::M && u.funct3 >= 4;
        const int pool = halt || invalid ? -1 : multiply ? 1 : divide ? 2 :
                         branch ? 3 : load ? 4 : store ? 5 : 0;
        const bool writes = !halt && !invalid && !store && !branch && u.allocDest && u.rd != 0;
        std::array<int, 7> slots{};
        bool can = lane < ROB_CAP - robUsed && (!writes || physical < phyCredits) &&
                   (!load || loads < loadCredits) && (!store || stores < storeCredits);
        for (unsigned which = 0; which < 7; ++which) {
          slots[which] = -1;
          const uint32_t mask = free[which] ? free[which] : reuse[which];
          for (unsigned row = 0; row < 32; ++row)
            if (slots[which] < 0 && (mask & (1u << row))) slots[which] = row;
        }
        if (pool >= 0) can &= slots[pool] >= 0;
        if (store) can &= slots[6] >= 0;
        if (u.type == RISC_V::M) can &= op != Operation::OP_INVALID;
        open = can && !halt;
        if (can) {
          auto &p = group.lanes[lane];
          p.valid = true;
          p.robTag = tag;
          p.allocDest = writes;
          p.phy = writes ? input.PRFModule.getFreeListSlot(phyHead) : InvalidPhy;
          p.isLoad = load; p.isStore = store; p.isHalt = halt;
          p.isControl = !halt && (u.type == RISC_V::J || (u.type == RISC_V::I && u.opcode == 0x67));
          p.pc = u.pc;
          auto source = [&](unsigned reg) {
            Operand value = resolveSrc(input, reg);
            if constexpr (ISSUE_WIDTH == 4)
              if (input.PRFModule.isOperandReady(value))
                value = {InvalidPhy, input.PRFModule.getOperandValue(value)};
            for (unsigned older = 0; older < lane; ++older) {
              const auto &prior = group.lanes[older];
              if (prior.allocDest && reg != 0 && reg == unsigned(prior.robEntry.dest)) {
                value = {prior.phy, 0};
                if constexpr (ISSUE_WIDTH == 4)
                  if (prior.isControl) value = {InvalidPhy, int32_t(prior.pc + 4u)};
              }
            }
            return value;
          };
          const Operand s1 = source(u.rs1), s2 = source(u.rs2);
          Operand left = s1, right = s2;
          if (upper) left = {InvalidPhy, (u.type == RISC_V::J || u.opcode == 0x17) ? int32_t(u.pc) : 0};
          if (upper || load || store || (pool == 0 && u.type != RISC_V::R)) right = {InvalidPhy, u.imm};
          auto payload = [&](auto &row) {
            row.free = false; row.op = op; row.robTag = tag;
            row.src1 = left; row.src2 = right;
          };
          if (pool == 0) { p.hasInteger = true; p.integerSlot = slots[0]; payload(p.integerRS); }
          if (multiply) { p.hasMultiply = true; p.multiplySlot = slots[1]; payload(p.multiplyRS); }
          if (divide) { p.hasDivide = true; p.divideSlot = slots[2]; payload(p.divideRS); }
          if (branch) { p.hasBranch = true; p.branchSlot = slots[3]; payload(p.branchRS); p.branchRS.imm = u.imm; p.branchRS.pc = u.pc; }
          if (load) { p.hasLoad = true; p.loadSlot = slots[4]; payload(p.loadRS); p.loadRS.memIndex = (input.LQModule.getTail() + loads) & LQ_MASK; }
          if (store) {
            p.hasStore = true; p.storeAddrSlot = slots[5]; p.storeValueSlot = slots[6];
            payload(p.storeAddrRS);
            p.storeAddrRS.memIndex = MEM_STORE_BIT | ((input.SQModule.getTail() + stores) & SQ_MASK);
            p.storeValueRS.free = false; p.storeValueRS.robTag = tag;
            p.storeValueRS.data = s2; p.storeValueRS.memIndex = p.storeAddrRS.memIndex;
          }
          if (load || store) {
            p.nBytes = (u.funct3 & 3) == 0 ? 1 : (u.funct3 & 3) == 1 ? 2 : 4;
            p.isUnsigned = load && (u.funct3 & 4);
          }
          auto &entry = p.robEntry;
          entry.type = p.isControl ? ROBType::LINK : store ? ROBType::STORE : branch ? ROBType::BRANCH : ROBType::REGISTER;
          entry.dest = writes || halt ? u.rd : 0;
          entry.pc = u.pc; entry.predictedPC = u.predictedPC; entry.ckptId = u.ckptId;
          entry.halt = halt; entry.exception = invalid; entry.isCommitReady = halt || invalid;
          entry.isIndirect = p.isControl && u.type == RISC_V::I;
          entry.isCall = p.isControl && (u.rd == 1 || u.rd == 5);
          entry.isRet = entry.isIndirect && (u.rs1 == 1 || u.rs1 == 5);
          entry.newPhy = p.phy;
          entry.oldPhy = writes ? input.RATModule.readRAT(u.rd) : InvalidPhy;
          for (unsigned older = 0; older < lane; ++older)
            if (writes && group.lanes[older].allocDest && group.lanes[older].robEntry.dest == u.rd)
              entry.oldPhy = group.lanes[older].phy;
          loads += load; stores += store;
          entry.lqTailSnapshot = (input.LQModule.getTail() + loads) & LQ_SEQ_MASK;
          entry.sqTailSnapshot = (input.SQModule.getTail() + stores) & SQ_SEQ_MASK;
          if (writes) { ++physical; phyHead = prfSeqNext(phyHead); }
          if (pool >= 0) { free[pool] &= ~(1u << slots[pool]); reuse[pool] &= ~(1u << slots[pool]); }
          if (store) { free[6] &= ~(1u << slots[6]); reuse[6] &= ~(1u << slots[6]); }
          tag = robNextTag(tag);
          ++group.count;
        }
      } else open = false;
    }
  }
  return group;
}
