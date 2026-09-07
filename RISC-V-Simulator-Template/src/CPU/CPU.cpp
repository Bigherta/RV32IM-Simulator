#include "../include/CPU.hpp"
#include "../include/util.hpp"
#include "common.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {
// Pre-decode scan of the last fetched word (see main tree CPU.cpp): classify
// the unconditional-jump family (jal / jalr) so RAS maintenance and BTB type
// training no longer depend on prediction-table hits. Conditional branches
// are deliberately excluded. Mirrors main-tree logic exactly.
FetchTypeInfo scanJump(bool valid, uint32_t raw, uint32_t pc) {
  FetchTypeInfo fi{};
  if (!valid)
    return fi;
  const uint32_t opcode = raw & 0x7F;
  const uint32_t rd = (raw >> 7) & 0x1F;
  const uint32_t rs1 = (raw >> 15) & 0x1F;
  const uint32_t funct3 = (raw >> 12) & 0x7;
  const bool rdLink = (rd == 1 || rd == 5);
  const bool rs1Link = (rs1 == 1 || rs1 == 5);
  fi.pc = pc;
  if (opcode == 0x6F) { // jal: direct call iff rd is a link register
    fi.isCall = rdLink;
    fi.jalTargetValid = true;
    uint32_t uoff = ((raw >> 31) & 1U) << 20;
    uoff |= ((raw >> 20) & 1U) << 11;
    for (int i = 12; i <= 19; ++i)
      uoff |= ((raw >> i) & 1U) << i;
    for (int i = 21; i <= 30; ++i)
      uoff |= ((raw >> i) & 1U) << (i - 20);
    const auto off = static_cast<int32_t>((uoff ^ 0x100000U) - 0x100000U);
    fi.jalTarget = static_cast<uint32_t>(static_cast<int32_t>(pc) + off);
    fi.valid = true;
  } else if (opcode == 0x67 && funct3 == 0) { // jalr
    fi.isCall = rdLink;                       // indirect call (push)
    fi.isRet = rs1Link && !rdLink;            // return (pop)
    fi.valid = true;
  }
  return fi;
}
} // namespace

CPU::CPU(Memory mem) : IMEMModule(mem), DMEMModule(mem) {
  wire();
  // Non-owning registration in the exact work() order of the former
  // hand-written run loop (module members outlive dark::CPU's pointers).
  dcpu.add_module(&FetchUnitModule);
  dcpu.add_module(&IMEMModule);
  dcpu.add_module(&ICacheModule);
  dcpu.add_module(&FQModule);
  dcpu.add_module(&LQModule);
  dcpu.add_module(&SQModule);
  dcpu.add_module(&ROBModule);
  dcpu.add_module(&PRFModule);
  dcpu.add_module(&ALUModule);
  dcpu.add_module(&MULModule);
  dcpu.add_module(&AGUModule);
  dcpu.add_module(&BRUModule);
  dcpu.add_module(&BPUModule);
  dcpu.add_module(&DCacheModule);
  dcpu.add_module(&DMEMModule);
  dcpu.add_module(&RSModule);
  dcpu.add_module(&RATModule);
  dcpu.add_module(&AluCDBArbiterModule);
  dcpu.add_module(&LqCDBArbiterModule);
  dcpu.add_module(&MulCDBModule);
  dcpu.add_module(&MemArbiterModule);
  dcpu.add_module(&DispatchArbiterModule);
  dcpu.add_module(&IssueArbiterModule);
  dcpu.add_module(&flushArbiter);
  dcpu.add_module(&DecodeUnitModule);
}

void CPU::wire() {
  // Wire FetchUnit's input wires once. needSquash/SquashPC come straight from
  // FlushArbiter (single producer, via its combinational member accessor);
  // haltSignal from ICache's combinational predicate; FetchValid/PredictPC
  // sample the BPU-owned prediction bundle (BPUOutput.fetchOut). Note: the
  // module inherits its Input, so wire the module instance
  // (FetchUnitModule.xxx), not a separate Input member.
  FetchUnitModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  FetchUnitModule.SquashPC = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashPC);
  };
  FetchUnitModule.FetchValid = [this]() {
    return static_cast<uint32_t>(BPUModule.fetchOut.valid);
  };
  FetchUnitModule.PredictPC = [this]() {
    return static_cast<uint32_t>(BPUModule.fetchOut.predictedPC);
  };
  FetchUnitModule.haltSignal = [this]() { return ICacheModule.isHaltSignal(); };

  // Wire InstructBuffer's (FQ) input wires once. icacheReturn* sample the
  // (still memcpy-snapshotted) ICache accessors; decodeFull samples the
  // DecodeUnit snapshot; haltFetched comes from the converted FetchUnit
  // Register.
  FQModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  FQModule.haltFetched = [this]() {
    return static_cast<uint32_t>(FetchUnitModule.haltFetched);
  };
  FQModule.icacheReturnReady = [this]() {
    return ICacheModule.isReturnReady();
  };
  FQModule.icacheReturnRaw = [this]() { return ICacheModule.returnRaw(); };
  FQModule.icacheReturnPC = [this]() { return ICacheModule.returnPC(); };
  FQModule.icacheReturnPredictedPC = [this]() {
    return ICacheModule.returnPredictPC();
  };
  FQModule.icacheReturnCkptId = [this]() {
    return ICacheModule.returnCkptId();
  };
  FQModule.decodeFull = [this]() { return DecodeUnitModule.isFull(); };

  // Wire the DecodeUnit's input wires once. fq* sample the converted
  // InstructBuffer's bridge accessors (_M_old); issueValid samples the
  // IssueArbiter's combinational Output.
  DecodeUnitModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  DecodeUnitModule.issueValid = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.valid);
  };
  DecodeUnitModule.fqEmpty = [this]() { return FQModule.isEmpty(); };
  DecodeUnitModule.fqHeadRaw = [this]() { return FQModule.headRaw(); };
  DecodeUnitModule.fqHeadPc = [this]() { return FQModule.headpc(); };
  DecodeUnitModule.fqHeadPredictedPC = [this]() {
    return FQModule.headPredictedPC();
  };
  DecodeUnitModule.fqHeadCkptId = [this]() { return FQModule.headCkptId(); };
  // Wire the IMEM's input wires once.
  IMEMModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  // original comb() gated imemFetch: a fetch that hits in the ICache must
  // NOT claim an IMEM line, and the claimed address is 16B-line aligned
  IMEMModule.fetchValid = [this]() {
    return static_cast<bool>(BPUModule.fetchOut.valid) &&
           !ICacheModule.hit(static_cast<uint32_t>(BPUModule.fetchOut.pc));
  };
  IMEMModule.fetchPC = [this]() {
    return static_cast<uint32_t>(BPUModule.fetchOut.pc) & ~0xFu;
  };
  IMEMModule.lineConsumed = [this]() { return IMEMModule.retValid(); };

  // Wire the ICache's input wires once. fetch* sample the BPU-owned
  // prediction bundle (ungated — the IMEM side carries the icacheHit gate);
  // popConsume is a combinational predicate over committed state; lineReturn
  // references IMEM's combinational return view (Wire-over-_M_old, no extra
  // pipeline stage).
  ICacheModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  ICacheModule.fetchValid = [this]() {
    return static_cast<uint32_t>(BPUModule.fetchOut.valid);
  };
  ICacheModule.fetchPC = [this]() {
    return static_cast<uint32_t>(BPUModule.fetchOut.pc);
  };
  ICacheModule.fetchPredictPC = [this]() {
    return static_cast<uint32_t>(BPUModule.fetchOut.predictedPC);
  };
  ICacheModule.fetchCkptId = [this]() {
    return static_cast<uint32_t>(BPUModule.fetchOut.ckptId);
  };
  ICacheModule.popConsume = [this]() {
    return ICacheModule.isReturnReady() &&
           !static_cast<bool>(FetchUnitModule.haltFetched) &&
           !FQModule.isFull();
  };
  ICacheModule.lineReturn.valid = [this]() { return IMEMModule.retValid(); };
  ICacheModule.lineReturn.lineAddr = [this]() {
    return IMEMModule.retLineAddr();
  };
  for (int w = 0; w < CACHE_BLOCK_CAP / 4; ++w) {
    ICacheModule.lineReturn.data[w] = [this, w]() {
      return IMEMModule.retWord(w);
    };
  }

  // ---- Wire the dual-CDB bus sources (producers stay bridges reading _M_old;
  // lsq* are gated here so an invalid CDBDetect index never reaches
  // getValue's throw; each source carries its own squash guard) ----
  AluCDBArbiterModule.aluEmpty = [this]() {
    return ALUModule.isEmpty() ? 1u : 0u;
  };
  AluCDBArbiterModule.aluValue = [this]() {
    return static_cast<uint32_t>(ALUModule.headValue());
  };
  AluCDBArbiterModule.aluRobTag = [this]() {
    return static_cast<uint32_t>(ALUModule.headRobTag());
  };
  AluCDBArbiterModule.aluIsControl = [this]() {
    return ALUModule.headIsControl() ? 1u : 0u;
  };
  AluCDBArbiterModule.squashNeed = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  AluCDBArbiterModule.squashTag = [this]() {
    return static_cast<uint32_t>(static_cast<uint32_t>(flushArbiter.SquashTag));
  };
  LqCDBArbiterModule.lsqValid = [this]() {
    return LQModule.CDBDetect() != -1 ? 1u : 0u;
  };
  LqCDBArbiterModule.lsqMemIndex = [this]() {
    auto d = LQModule.CDBDetect();
    return d != -1 ? static_cast<uint32_t>(d) : 0u;
  };
  LqCDBArbiterModule.lsqRobTag = [this]() {
    auto d = LQModule.CDBDetect();
    return d != -1 ? static_cast<uint32_t>(LQModule.getRobTag(d)) : 0u;
  };
  LqCDBArbiterModule.lsqValue = [this]() {
    auto d = LQModule.CDBDetect();
    return d != -1 ? static_cast<uint32_t>(LQModule.getValue(d)) : 0u;
  };
  LqCDBArbiterModule.squashNeed = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  LqCDBArbiterModule.squashTag = [this]() {
    return static_cast<uint32_t>(static_cast<uint32_t>(flushArbiter.SquashTag));
  };
  // MulCDB: the MUL unit's dedicated result bus. Empty/value/tag sample the
  // MUL head bridges (_M_old); the squash guard lives in MulCDB::mulLive.
  MulCDBModule.mulEmpty = [this]() {
    return MULModule.isEmpty() ? 1u : 0u;
  };
  MulCDBModule.mulValue = [this]() {
    return static_cast<uint32_t>(MULModule.headValue());
  };
  MulCDBModule.mulRobTag = [this]() {
    return static_cast<uint32_t>(MULModule.headRobTag());
  };
  MulCDBModule.squashNeed = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  MulCDBModule.squashTag = [this]() {
    return static_cast<uint32_t>(static_cast<uint32_t>(flushArbiter.SquashTag));
  };

  // ---- Wire MemArbiter's Input Wires (producers stay bridges reading
  // _M_old; SQ head addr/value gated by ready flags and LQ load payload gated
  // by loadValid so the un-ready throw paths are never reached -- the ready-
  // flag gate is a superset-safe call of the original storeSelected-only
  // call sites, pure reads with no side effects) ----
  MemArbiterModule.dmemBusy = [this]() {
    return static_cast<uint32_t>(DCacheModule.isBusy());
  };
  MemArbiterModule.sqEmpty = [this]() { return SQModule.isEmpty() ? 1u : 0u; };
  MemArbiterModule.sqHeadRobTag = [this]() {
    return static_cast<uint32_t>(SQModule.headRobTag());
  };
  MemArbiterModule.sqHead = [this]() {
    return static_cast<uint32_t>(SQModule.getHead());
  };
  MemArbiterModule.sqHeadNEnc = [this]() {
    int n = SQModule.getNBytes(SQModule.getHead());
    return static_cast<uint32_t>(n == 1 ? 0 : (n == 2 ? 1 : 2));
  };
  MemArbiterModule.sqHeadAddr = [this]() {
    return (!SQModule.isEmpty() && SQModule.isAddressReady(SQModule.getHead()))
               ? static_cast<uint32_t>(SQModule.getAddress(SQModule.getHead()))
               : 0u;
  };
  MemArbiterModule.sqHeadValue = [this]() {
    return (!SQModule.isEmpty() && SQModule.isValueReady(SQModule.getHead()))
               ? static_cast<uint32_t>(SQModule.getValue(SQModule.getHead()))
               : 0u;
  };
  MemArbiterModule.robHeadEmpty = [this]() {
    return static_cast<bool>(ROBModule.headView.isEmpty) ? 1u : 0u;
  };
  MemArbiterModule.robHeadTag = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.head);
  };
  MemArbiterModule.robHeadCommitReady = [this]() {
    return static_cast<bool>(
               ROBModule.entry
                   .isCommitReady[static_cast<uint32_t>(SQModule.headRobTag()) &
                                  0x3F])
               ? 1u
               : 0u;
  };
  MemArbiterModule.loadValid = [this]() {
    return LQModule.LoadDetect() != 0xFFFFFFFF ? 1u : 0u;
  };
  MemArbiterModule.loadIndex = [this]() {
    auto d = LQModule.LoadDetect();
    return d != 0xFFFFFFFF ? static_cast<uint32_t>(d) : 0u;
  };
  MemArbiterModule.loadAddr = [this]() {
    auto d = LQModule.LoadDetect();
    return d != 0xFFFFFFFF ? static_cast<uint32_t>(LQModule.getAddress(d)) : 0u;
  };
  MemArbiterModule.loadRobTag = [this]() {
    auto d = LQModule.LoadDetect();
    return d != 0xFFFFFFFF ? static_cast<uint32_t>(LQModule.getRobTag(d)) : 0u;
  };
  MemArbiterModule.loadIsSigned = [this]() {
    auto d = LQModule.LoadDetect();
    return d != 0xFFFFFFFF ? (LQModule.getIsUnsigned(d) ? 0u : 1u) : 0u;
  };
  MemArbiterModule.loadNEnc = [this]() {
    auto d = LQModule.LoadDetect();
    if (d == 0xFFFFFFFF)
      return 0u;
    int n = LQModule.getNBytes(d);
    return static_cast<uint32_t>(n == 1 ? 0 : (n == 2 ? 1 : 2));
  };
  MemArbiterModule.loadCanDispatch = [this]() {
    auto d = LQModule.LoadDetect();
    return d != 0xFFFFFFFF && SQModule.canDispatchLoad(LQModule.getAddress(d),
                                                       LQModule.getRobTag(d))
               ? 1u
               : 0u;
  };
  MemArbiterModule.squashNeed = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  MemArbiterModule.squashTag = [this]() {
    return static_cast<uint32_t>(static_cast<uint32_t>(flushArbiter.SquashTag));
  };

  // ---- Wire DispatchArbiter's Input Wires (ports are buses: RS slot fields
  // + PRF ready bitmap; src tags go through Wire<7> so the 8th sentinel bit
  // is clipped at the wiring site and prdReady indexing stays in-bounds) ----
  DispatchArbiterModule.aluFull = [this]() {
    return ALUModule.isFull() ? 1u : 0u;
  };
  DispatchArbiterModule.aguFull = [this]() {
    return AGUModule.isFull() ? 1u : 0u;
  };
  DispatchArbiterModule.bruFull = [this]() {
    return BRUModule.isFull() ? 1u : 0u;
  };
  for (int i = 0; i < INTEGERRS_CAP; ++i) {
    DispatchArbiterModule.intBusy[i] = [this, i]() {
      return RSModule.isIntFree(i) ? 0u : 1u;
    };
    DispatchArbiterModule.intSrc1Tag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getIntSrc1(i).tag);
    };
    DispatchArbiterModule.intSrc2Tag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getIntSrc2(i).tag);
    };
    DispatchArbiterModule.intRobTag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getIntRobTag(i));
    };
  }
  for (int i = 0; i < LOADRS_CAP; ++i) {
    DispatchArbiterModule.loadBusy[i] = [this, i]() {
      return RSModule.isLoadFree(i) ? 0u : 1u;
    };
    DispatchArbiterModule.loadSrc1Tag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getLoadSrc1(i).tag);
    };
    DispatchArbiterModule.loadSrc2Tag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getLoadSrc2(i).tag);
    };
    DispatchArbiterModule.loadRobTag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getLoadRobTag(i));
    };
  }
  for (int i = 0; i < STORERS_CAP; ++i) {
    DispatchArbiterModule.saBusy[i] = [this, i]() {
      return RSModule.isSaFree(i) ? 0u : 1u;
    };
    DispatchArbiterModule.saSrc1Tag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getSaSrc1(i).tag);
    };
    DispatchArbiterModule.saRobTag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getSaRobTag(i));
    };
  }
  for (int i = 0; i < BRANCHRS_CAP; ++i) {
    DispatchArbiterModule.brBusy[i] = [this, i]() {
      return RSModule.isBrFree(i) ? 0u : 1u;
    };
    DispatchArbiterModule.brSrc1Tag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getBrSrc1(i).tag);
    };
    DispatchArbiterModule.brSrc2Tag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getBrSrc2(i).tag);
    };
    DispatchArbiterModule.brRobTag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getBrRobTag(i));
    };
  }
  // MUL channel buses: multiplyRS slot fields + MUL isFull gate (mirrors the
  // ALU channel over the dedicated multiply pool).
  DispatchArbiterModule.mulFull = [this]() {
    return MULModule.isFull() ? 1u : 0u;
  };
  for (int i = 0; i < MULTIPLYRS_CAP; ++i) {
    DispatchArbiterModule.mulBusy[i] = [this, i]() {
      return RSModule.isMulFree(i) ? 0u : 1u;
    };
    DispatchArbiterModule.mulSrc1Tag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getMulSrc1(i).tag);
    };
    DispatchArbiterModule.mulSrc2Tag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getMulSrc2(i).tag);
    };
    DispatchArbiterModule.mulRobTag[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getMulRobTag(i));
    };
  }
  for (int i = 0; i < PRF_CAP; ++i) {
    DispatchArbiterModule.prdReady[i] = [this, i]() {
      return PRFModule.isReady(i) ? 1u : 0u;
    };
  }
  DispatchArbiterModule.squashNeed = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  DispatchArbiterModule.squashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };

  // Wire the ALU's input wires once: dispatch payload samples the
  // DispatchArbiter's alu grant; RS/PRF reads guard on dispatch.valid.
  ALUModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  ALUModule.SquashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };
  ALUModule.dispatchValid = [this]() {
    return static_cast<bool>(DispatchArbiterModule.alu.valid);
  };
  ALUModule.src1Value = [this]() {
    const auto &d = DispatchArbiterModule.alu;
    return static_cast<bool>(d.valid)
               ? static_cast<uint32_t>(PRFModule.getOperandValue(
                     RSModule.getIntSrc1(static_cast<uint32_t>(d.rsIndex))))
               : 0u;
  };
  ALUModule.src2Value = [this]() {
    const auto &d = DispatchArbiterModule.alu;
    return static_cast<bool>(d.valid)
               ? static_cast<uint32_t>(PRFModule.getOperandValue(
                     RSModule.getIntSrc2(static_cast<uint32_t>(d.rsIndex))))
               : 0u;
  };
  ALUModule.op = [this]() {
    const auto &d = DispatchArbiterModule.alu;
    return static_cast<bool>(d.valid) ? static_cast<uint32_t>(RSModule.getIntOp(
                                            static_cast<uint32_t>(d.rsIndex)))
                                      : 0u;
  };
  ALUModule.dispatchRobTag = [this]() {
    return static_cast<uint32_t>(DispatchArbiterModule.alu.robTag);
  };
  ALUModule.cdbValid = [this]() { return AluCDBArbiterModule.valid ? 1u : 0u; };
  ALUModule.cdbRobTag = [this]() {
    return static_cast<uint32_t>(AluCDBArbiterModule.robTag);
  };

  // Wire the MUL's input wires once: dispatch payload samples the
  // DispatchArbiter's mul grant; RS/PRF reads guard on dispatch.valid; the
  // CDB drain samples the dedicated MulCDB bus.
  MULModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  MULModule.SquashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };
  MULModule.dispatchValid = [this]() {
    return static_cast<bool>(DispatchArbiterModule.mul.valid);
  };
  MULModule.src1Value = [this]() {
    const auto &d = DispatchArbiterModule.mul;
    return static_cast<bool>(d.valid)
               ? static_cast<uint32_t>(PRFModule.getOperandValue(
                     RSModule.getMulSrc1(static_cast<uint32_t>(d.rsIndex))))
               : 0u;
  };
  MULModule.src2Value = [this]() {
    const auto &d = DispatchArbiterModule.mul;
    return static_cast<bool>(d.valid)
               ? static_cast<uint32_t>(PRFModule.getOperandValue(
                     RSModule.getMulSrc2(static_cast<uint32_t>(d.rsIndex))))
               : 0u;
  };
  MULModule.op = [this]() {
    const auto &d = DispatchArbiterModule.mul;
    return static_cast<bool>(d.valid) ? static_cast<uint32_t>(RSModule.getMulOp(
                                            static_cast<uint32_t>(d.rsIndex)))
                                      : 0u;
  };
  MULModule.dispatchRobTag = [this]() {
    return static_cast<uint32_t>(DispatchArbiterModule.mul.robTag);
  };
  MULModule.cdbValid = [this]() { return MulCDBModule.valid ? 1u : 0u; };
  MULModule.cdbRobTag = [this]() {
    return static_cast<uint32_t>(MulCDBModule.robTag);
  };

  // Wire the AGU's input wires once: the load/store RS array choice is made
  // here (rsType on the DispatchArbiter's agu grant), guarded by
  // dispatch.valid.
  AGUModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  AGUModule.SquashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };
  AGUModule.dispatchValid = [this]() {
    return static_cast<bool>(DispatchArbiterModule.agu.valid);
  };
  AGUModule.src1Value = [this]() {
    const auto &d = DispatchArbiterModule.agu;
    if (!static_cast<bool>(d.valid))
      return 0u;
    if (static_cast<uint32_t>(d.rsType) == static_cast<uint32_t>(RSType::Load))
      return static_cast<uint32_t>(PRFModule.getOperandValue(
          RSModule.getLoadSrc1(static_cast<uint32_t>(d.rsIndex))));
    return static_cast<uint32_t>(PRFModule.getOperandValue(
        RSModule.getSaSrc1(static_cast<uint32_t>(d.rsIndex))));
  };
  AGUModule.src2Value = [this]() {
    const auto &d = DispatchArbiterModule.agu;
    if (!static_cast<bool>(d.valid))
      return 0u;
    if (static_cast<uint32_t>(d.rsType) == static_cast<uint32_t>(RSType::Load))
      return static_cast<uint32_t>(PRFModule.getOperandValue(
          RSModule.getLoadSrc2(static_cast<uint32_t>(d.rsIndex))));
    return static_cast<uint32_t>(PRFModule.getOperandValue(
        RSModule.getSaSrc2(static_cast<uint32_t>(d.rsIndex))));
  };
  AGUModule.memIndex = [this]() {
    const auto &d = DispatchArbiterModule.agu;
    if (!static_cast<bool>(d.valid))
      return 0u;
    if (static_cast<uint32_t>(d.rsType) == static_cast<uint32_t>(RSType::Load))
      return static_cast<uint32_t>(
          RSModule.getLoadMemIndex(static_cast<uint32_t>(d.rsIndex)));
    return static_cast<uint32_t>(
        RSModule.getSaMemIndex(static_cast<uint32_t>(d.rsIndex)));
  };
  AGUModule.dispatchRobTag = [this]() {
    return static_cast<uint32_t>(DispatchArbiterModule.agu.robTag);
  };

  // Wire the BRU's input wires once: payload comes from the branchRS slot
  // selected by the DispatchArbiter's bru grant, guarded by dispatch.valid.
  BRUModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  BRUModule.SquashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };
  BRUModule.dispatchValid = [this]() {
    return static_cast<bool>(DispatchArbiterModule.bru.valid);
  };
  BRUModule.src1Value = [this]() {
    const auto &d = DispatchArbiterModule.bru;
    return static_cast<bool>(d.valid)
               ? static_cast<uint32_t>(PRFModule.getOperandValue(
                     RSModule.getBrSrc1(static_cast<uint32_t>(d.rsIndex))))
               : 0u;
  };
  BRUModule.src2Value = [this]() {
    const auto &d = DispatchArbiterModule.bru;
    return static_cast<bool>(d.valid)
               ? static_cast<uint32_t>(PRFModule.getOperandValue(
                     RSModule.getBrSrc2(static_cast<uint32_t>(d.rsIndex))))
               : 0u;
  };
  BRUModule.pc = [this]() {
    const auto &d = DispatchArbiterModule.bru;
    return static_cast<bool>(d.valid) ? static_cast<uint32_t>(RSModule.getBrPc(
                                            static_cast<uint32_t>(d.rsIndex)))
                                      : 0u;
  };
  BRUModule.imm = [this]() {
    const auto &d = DispatchArbiterModule.bru;
    return static_cast<bool>(d.valid) ? static_cast<uint32_t>(RSModule.getBrImm(
                                            static_cast<uint32_t>(d.rsIndex)))
                                      : 0u;
  };
  BRUModule.op = [this]() {
    const auto &d = DispatchArbiterModule.bru;
    return static_cast<bool>(d.valid) ? static_cast<uint32_t>(RSModule.getBrOp(
                                            static_cast<uint32_t>(d.rsIndex)))
                                      : 0u;
  };
  BRUModule.dispatchRobTag = [this]() {
    return static_cast<uint32_t>(DispatchArbiterModule.bru.robTag);
  };

  // Wire PRF's Input Wires (Verilog-style explicit ports)
  PRFModule.squash.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  PRFModule.squash.SquashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };
  PRFModule.squash.CkptId = [this]() {
    return static_cast<uint32_t>(flushArbiter.CkptId);
  };
  // dual-CDB write ports: ALU group keeps isControl (PRF never writes control
  // results); the LQ group omits it (loads are never control ops). newPhy is
  // looked up from the ROB entry by tag, valid-gated (a broadcast only ever
  // targets an in-flight entry's rename).
  PRFModule.cdbOfALU.cdbValid = [this]() {
    return AluCDBArbiterModule.valid ? 1u : 0u;
  };
  PRFModule.cdbOfALU.cdbValue = [this]() {
    return static_cast<uint32_t>(AluCDBArbiterModule.value);
  };
  PRFModule.cdbOfALU.cdbRobTag = [this]() {
    return static_cast<uint32_t>(AluCDBArbiterModule.robTag);
  };
  PRFModule.cdbOfALU.cdbIsControl = [this]() {
    return AluCDBArbiterModule.isControl ? 1u : 0u;
  };
  PRFModule.cdbOfALU.cdbNewPhy = [this]() {
    if (!static_cast<bool>(AluCDBArbiterModule.valid))
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(
        ROBModule.entry
            .newPhy[static_cast<uint32_t>(AluCDBArbiterModule.robTag) & 0x3F]);
  };
  PRFModule.cdbOfLQ.cdbValid = [this]() {
    return LqCDBArbiterModule.valid ? 1u : 0u;
  };
  PRFModule.cdbOfLQ.cdbValue = [this]() {
    return static_cast<uint32_t>(LqCDBArbiterModule.value);
  };
  PRFModule.cdbOfLQ.cdbRobTag = [this]() {
    return static_cast<uint32_t>(LqCDBArbiterModule.robTag);
  };
  PRFModule.cdbOfLQ.cdbNewPhy = [this]() {
    if (!static_cast<bool>(LqCDBArbiterModule.valid))
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(
        ROBModule.entry
            .newPhy[static_cast<uint32_t>(LqCDBArbiterModule.robTag) & 0x3F]);
  };
  // MUL write port: never a control op, so no isControl wire (loads and
  // multiplies share that port saving).
  PRFModule.cdbOfMUL.cdbValid = [this]() {
    return MulCDBModule.valid ? 1u : 0u;
  };
  PRFModule.cdbOfMUL.cdbValue = [this]() {
    return static_cast<uint32_t>(MulCDBModule.value);
  };
  PRFModule.cdbOfMUL.cdbRobTag = [this]() {
    return static_cast<uint32_t>(MulCDBModule.robTag);
  };
  PRFModule.cdbOfMUL.cdbNewPhy = [this]() {
    if (!static_cast<bool>(MulCDBModule.valid))
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(
        ROBModule.entry.newPhy[static_cast<uint32_t>(MulCDBModule.robTag) &
                               0x3F]);
  };
  PRFModule.issue.issueValid = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.valid);
  };
  PRFModule.issue.issuePhy = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.phy);
  };
  PRFModule.issue.issueAllocDest = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.allocDest);
  };
  PRFModule.issue.issuePC = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.pc);
  };
  PRFModule.issue.issueIsControl = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.isControl);
  };
  PRFModule.issue.issueCkptId = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.ckptId);
  };
  PRFModule.rob.robHead = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.head);
  };
  PRFModule.rob.isRobEmpty = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.isEmpty);
  };
  PRFModule.rob.isRobHeadCommitReady = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.isHeadCommitReady);
  };
  PRFModule.rob.robHeadIsHalt = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.isHeadHalt);
  };
  PRFModule.rob.robHeadType = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.headType);
  };
  PRFModule.rob.robHeadOldPhy = [this]() {
    if (static_cast<bool>(ROBModule.headView.isEmpty))
      return static_cast<uint32_t>(InvalidPhy);
    return static_cast<uint32_t>(
        ROBModule.entry
            .oldPhy[static_cast<uint32_t>(ROBModule.headView.head) & 0x3F]);
  };

  // Wire RAT's Input Wires
  RATModule.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  RATModule.SquashCkptId = [this]() {
    return static_cast<uint32_t>(flushArbiter.CkptId);
  };
  RATModule.issueValid = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.valid);
  };
  RATModule.issuePhy = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.phy);
  };
  RATModule.issueDest = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.dest);
  };
  RATModule.issueAllocDest = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.allocDest);
  };
  RATModule.issueCkptId = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.ckptId);
  };

  // ---- Wire the IssueArbiter's Input Wires (stateless issue arbiter; the
  // former build() read these same producer bridges from comb, so every wire
  // is same-source same-cycle by construction). The RS busy bitmaps feed the
  // module's verbatim first-fit scans; the RAT operand views feed the
  // module's verbatim resolveSrc (assert-gated, NOT superset-safe). ----
  IssueArbiterModule.dec.isEmpty = [this]() {
    return DecodeUnitModule.isEmpty() ? 1u : 0u;
  };
  IssueArbiterModule.dec.type = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headType());
  };
  IssueArbiterModule.dec.opcode = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headOpcode());
  };
  IssueArbiterModule.dec.funct3 = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headFunct3());
  };
  IssueArbiterModule.dec.funct7 = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headFunct7());
  };
  IssueArbiterModule.dec.rd = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headRd());
  };
  IssueArbiterModule.dec.rs1 = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headRs1());
  };
  IssueArbiterModule.dec.rs2 = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headRs2());
  };
  IssueArbiterModule.dec.imm = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headImm());
  };
  IssueArbiterModule.dec.pc = [this]() { return DecodeUnitModule.headPc(); };
  IssueArbiterModule.dec.isHalt = [this]() {
    return DecodeUnitModule.headIsHalt() ? 1u : 0u;
  };
  IssueArbiterModule.dec.allocDest = [this]() {
    return DecodeUnitModule.headAllocDest() ? 1u : 0u;
  };
  IssueArbiterModule.dec.predictedPC = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headPredictedPC());
  };
  IssueArbiterModule.dec.ckptId = [this]() {
    return static_cast<uint32_t>(DecodeUnitModule.headCkptId());
  };
  IssueArbiterModule.rob.isFull = [this]() {
    return ROBModule.isFull() ? 1u : 0u;
  };
  IssueArbiterModule.rob.nextTag = [this]() { return ROBModule.getNextTag(); };
  for (int i = 0; i < INTEGERRS_CAP; ++i) {
    IssueArbiterModule.rs.intBusy[i] = [this, i]() {
      return RSModule.isIntFree(i) ? 0u : 1u;
    };
  }
  for (int i = 0; i < LOADRS_CAP; ++i) {
    IssueArbiterModule.rs.loadBusy[i] = [this, i]() {
      return RSModule.isLoadFree(i) ? 0u : 1u;
    };
  }
  for (int i = 0; i < STORERS_CAP; ++i) {
    IssueArbiterModule.rs.saBusy[i] = [this, i]() {
      return RSModule.isSaFree(i) ? 0u : 1u;
    };
    IssueArbiterModule.rs.svBusy[i] = [this, i]() {
      return RSModule.isSvFree(i) ? 0u : 1u;
    };
  }
  for (int i = 0; i < BRANCHRS_CAP; ++i) {
    IssueArbiterModule.rs.brBusy[i] = [this, i]() {
      return RSModule.isBrFree(i) ? 0u : 1u;
    };
  }
  for (int i = 0; i < MULTIPLYRS_CAP; ++i) {
    IssueArbiterModule.rs.mulBusy[i] = [this, i]() {
      return RSModule.isMulFree(i) ? 0u : 1u;
    };
  }
  IssueArbiterModule.prf.freeListEmpty = [this]() {
    return PRFModule.isFreeListEmpty() ? 1u : 0u;
  };
  IssueArbiterModule.prf.freePhy = [this]() {
    return static_cast<uint32_t>(
        PRFModule.getFreeListSlot(PRFModule.getHeadSeq()));
  };
  IssueArbiterModule.lsq.lqFull = [this]() {
    return LQModule.isFull() ? 1u : 0u;
  };
  IssueArbiterModule.lsq.sqFull = [this]() {
    return SQModule.isFull() ? 1u : 0u;
  };
  IssueArbiterModule.lsq.lqTail = [this]() {
    return static_cast<uint32_t>(LQModule.getTail());
  };
  IssueArbiterModule.lsq.sqTail = [this]() {
    return static_cast<uint32_t>(SQModule.getTail());
  };
  IssueArbiterModule.lsq.lqTailSnapshot = [this]() {
    return static_cast<uint32_t>(LQModule.getTailSnapshot());
  };
  IssueArbiterModule.lsq.sqTailSnapshot = [this]() {
    return static_cast<uint32_t>(SQModule.getTailSnapshot());
  };
  IssueArbiterModule.rat.s1.ready = [this]() {
    return RATModule.readOperand(DecodeUnitModule.headRs1()).ready ? 1u : 0u;
  };
  IssueArbiterModule.rat.s1.phy = [this]() {
    return RATModule.readOperand(DecodeUnitModule.headRs1()).phyRegIndex;
  };
  IssueArbiterModule.rat.s1.value = [this]() {
    return static_cast<uint32_t>(
        RATModule.readOperand(DecodeUnitModule.headRs1()).value);
  };
  IssueArbiterModule.rat.s2.ready = [this]() {
    return RATModule.readOperand(DecodeUnitModule.headRs2()).ready ? 1u : 0u;
  };
  IssueArbiterModule.rat.s2.phy = [this]() {
    return RATModule.readOperand(DecodeUnitModule.headRs2()).phyRegIndex;
  };
  IssueArbiterModule.rat.s2.value = [this]() {
    return static_cast<uint32_t>(
        RATModule.readOperand(DecodeUnitModule.headRs2()).value);
  };
  IssueArbiterModule.rat.rdOldPhy = [this]() {
    return RATModule.readRAT_PRF(DecodeUnitModule.headRd());
  };
  IssueArbiterModule.squashNeed = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  // Wire RS's Input Wires: issue selection + flattened push payloads sample
  // the IssueArbiter's Output groups; dispatch release reads the
  // DispatchArbiter's grants; squash reads FlushArbiter; the storeValue
  // ready-release reads PRF readiness over this RS's own committed data.
  RSModule.sel.valid = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.valid);
  };
  RSModule.sel.hasInteger = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.hasInteger);
  };
  RSModule.sel.hasLoad = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.hasLoad);
  };
  RSModule.sel.hasStore = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.hasStore);
  };
  RSModule.sel.hasBranch = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.hasBranch);
  };
  RSModule.sel.hasMultiply = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.hasMultiply);
  };
  RSModule.sel.integerSlot = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.integerSlot);
  };
  RSModule.sel.loadSlot = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.loadSlot);
  };
  RSModule.sel.storeAddrSlot = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.storeAddrSlot);
  };
  RSModule.sel.storeValueSlot = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.storeValueSlot);
  };
  RSModule.sel.branchSlot = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.branchSlot);
  };
  RSModule.sel.multiplySlot = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.select.multiplySlot);
  };
  RSModule.data.intP.op = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.intP.op);
  };
  RSModule.data.intP.s1Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.intP.s1Tag);
  };
  RSModule.data.intP.s1Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.intP.s1Imm);
  };
  RSModule.data.intP.s2Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.intP.s2Tag);
  };
  RSModule.data.intP.s2Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.intP.s2Imm);
  };
  RSModule.data.intP.robTag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.intP.robTag);
  };
  RSModule.data.loadP.op = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.loadP.op);
  };
  RSModule.data.loadP.s1Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.loadP.s1Tag);
  };
  RSModule.data.loadP.s1Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.loadP.s1Imm);
  };
  RSModule.data.loadP.s2Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.loadP.s2Tag);
  };
  RSModule.data.loadP.s2Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.loadP.s2Imm);
  };
  RSModule.data.loadP.robTag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.loadP.robTag);
  };
  RSModule.data.loadP.memIndex = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.loadP.memIndex);
  };
  RSModule.data.saP.op = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.saP.op);
  };
  RSModule.data.saP.s1Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.saP.s1Tag);
  };
  RSModule.data.saP.s1Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.saP.s1Imm);
  };
  RSModule.data.saP.s2Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.saP.s2Tag);
  };
  RSModule.data.saP.s2Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.saP.s2Imm);
  };
  RSModule.data.saP.robTag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.saP.robTag);
  };
  RSModule.data.saP.memIndex = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.saP.memIndex);
  };
  RSModule.data.svP.dataTag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.svP.dataTag);
  };
  RSModule.data.svP.dataImm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.svP.dataImm);
  };
  RSModule.data.svP.robTag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.svP.robTag);
  };
  RSModule.data.svP.memIndex = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.svP.memIndex);
  };
  RSModule.data.brP.op = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.brP.op);
  };
  RSModule.data.brP.s1Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.brP.s1Tag);
  };
  RSModule.data.brP.s1Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.brP.s1Imm);
  };
  RSModule.data.brP.s2Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.brP.s2Tag);
  };
  RSModule.data.brP.s2Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.brP.s2Imm);
  };
  RSModule.data.brP.robTag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.brP.robTag);
  };
  RSModule.data.brP.imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.brP.imm);
  };
  RSModule.data.brP.pc = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.brP.pc);
  };
  RSModule.data.mulP.op = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.mulP.op);
  };
  RSModule.data.mulP.s1Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.mulP.s1Tag);
  };
  RSModule.data.mulP.s1Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.mulP.s1Imm);
  };
  RSModule.data.mulP.s2Tag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.mulP.s2Tag);
  };
  RSModule.data.mulP.s2Imm = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.mulP.s2Imm);
  };
  RSModule.data.mulP.robTag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.mulP.robTag);
  };
  RSModule.dispatch.aluValid = [this]() {
    return static_cast<bool>(DispatchArbiterModule.alu.valid);
  };
  RSModule.dispatch.aluIdx = [this]() {
    return static_cast<uint32_t>(DispatchArbiterModule.alu.rsIndex);
  };
  RSModule.dispatch.aguValid = [this]() {
    return static_cast<bool>(DispatchArbiterModule.agu.valid);
  };
  RSModule.dispatch.aguIdx = [this]() {
    return static_cast<uint32_t>(DispatchArbiterModule.agu.rsIndex);
  };
  RSModule.dispatch.aguIsLoad = [this]() {
    return static_cast<uint32_t>(DispatchArbiterModule.agu.rsType) ==
           static_cast<uint32_t>(RSType::Load);
  };
  RSModule.dispatch.bruValid = [this]() {
    return static_cast<bool>(DispatchArbiterModule.bru.valid);
  };
  RSModule.dispatch.bruIdx = [this]() {
    return static_cast<uint32_t>(DispatchArbiterModule.bru.rsIndex);
  };
  RSModule.dispatch.mulValid = [this]() {
    return static_cast<bool>(DispatchArbiterModule.mul.valid);
  };
  RSModule.dispatch.mulIdx = [this]() {
    return static_cast<uint32_t>(DispatchArbiterModule.mul.rsIndex);
  };
  RSModule.squash.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  RSModule.squash.SquashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };
  for (int i = 0; i < STORERS_CAP; ++i) {
    RSModule.prf.svReady[i] = [this, i]() {
      return PRFModule.isOperandReady(RSModule.getSvData(i));
    };
  }

  // ---- Wire DCache's Input Wires (decision from the MemArbiter Output;
  // squash from FlushArbiter; DMEM completion observed through the DMEM
  // bridge accessors reading _M_old -- the DCache is now DMEM's only
  // client) ----
  DCacheModule.squashNeed = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  DCacheModule.squashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };
  DCacheModule.squashPC = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashPC);
  };
  DCacheModule.squashCkptId = [this]() {
    return static_cast<uint32_t>(flushArbiter.CkptId);
  };
  DCacheModule.decisionValid = [this]() {
    return MemArbiterModule.valid ? 1u : 0u;
  };
  DCacheModule.decisionOp = [this]() {
    return static_cast<uint32_t>(MemArbiterModule.op);
  };
  DCacheModule.decisionValue = [this]() {
    return static_cast<uint32_t>(MemArbiterModule.value);
  };
  DCacheModule.decisionAddr = [this]() {
    return static_cast<uint32_t>(MemArbiterModule.address);
  };
  DCacheModule.decisionIsSigned = [this]() {
    return MemArbiterModule.isSigned ? 1u : 0u;
  };
  DCacheModule.decisionNBytes = [this]() {
    return static_cast<uint32_t>(MemArbiterModule.nEnc);
  };
  DCacheModule.decisionRobTag = [this]() {
    return static_cast<uint32_t>(MemArbiterModule.robTag);
  };
  DCacheModule.decisionMemIndex = [this]() {
    return static_cast<uint32_t>(MemArbiterModule.memIndex);
  };
  DCacheModule.dmemReadBusy = [this]() {
    return DMEMModule.isReadBusy() ? 1u : 0u;
  };
  DCacheModule.dmemWriteBusy = [this]() {
    return DMEMModule.isWriteBusy() ? 1u : 0u;
  };
  DCacheModule.dmemReplyReady = [this]() {
    return DMEMModule.isReplyReady() ? 1u : 0u;
  };
  for (int i = 0; i < DCACHE_BLOCK_CAP; ++i) {
    DCacheModule.dmemLineData[i] = [this, i]() {
      return DMEMModule.lineByte(i);
    };
  }

  // Wire the DMEM's Input Wires (dual-port line access: readValid/readAddress
  // for fills, writeValid/writeAddress/writeLineData for dirty writebacks --
  // the DCache is the only producer).
  DMEMModule.DMEMInput::readValid = [this]() {
    return static_cast<uint32_t>(DCacheModule.reqReadValid);
  };
  DMEMModule.DMEMInput::readAddress = [this]() {
    return static_cast<uint32_t>(DCacheModule.reqReadAddr);
  };
  DMEMModule.DMEMInput::writeValid = [this]() {
    return static_cast<uint32_t>(DCacheModule.reqWriteValid);
  };
  DMEMModule.DMEMInput::writeAddress = [this]() {
    return static_cast<uint32_t>(DCacheModule.reqWriteAddr);
  };
  for (int i = 0; i < DCACHE_BLOCK_CAP; ++i) {
    DMEMModule.DMEMInput::writeLineData[i] = [this, i]() {
      return static_cast<uint32_t>(DCacheModule.reqWriteLineData[i]);
    };
  }

  LQModule.squash.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  LQModule.squash.SquashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };
  LQModule.issue.issueValid = [this]() {
    return (static_cast<bool>(IssueArbiterModule.core.valid) &&
            static_cast<bool>(IssueArbiterModule.core.isLoad))
               ? 1u
               : 0u;
  };
  LQModule.issue.issueLoad = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.isLoad);
  };
  LQModule.issue.issueTag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.robTag);
  };
  LQModule.issue.issueBytes = [this]() {
    const uint32_t n = static_cast<uint32_t>(IssueArbiterModule.core.nBytes);
    return (n == 1 ? 0u : (n == 2 ? 1u : 2u));
  };
  LQModule.issue.issueIsUnsigned = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.isUnsigned);
  };
  LQModule.cdb.lsqSetCDB = [this]() {
    // lqCDB.valid already embeds the squash guard on the lsq candidate
    // (LqCDBArbiter passes only candidates older than the squash point), so
    // the former re-check here is a redundant idempotent gate.
    return LqCDBArbiterModule.valid ? 1u : 0u;
  };
  LQModule.cdb.cdbMemIndex = [this]() {
    return static_cast<uint32_t>(LqCDBArbiterModule.memIndex);
  };
  LQModule.agu.isAGUEmpty = [this]() { return AGUModule.isEmpty(); };
  LQModule.agu.aguHeadMemIndex = [this]() { return AGUModule.headMemIndex(); };
  LQModule.agu.aguHeadRobTag = [this]() { return AGUModule.headRobTag(); };
  LQModule.agu.aguHeadValue = [this]() {
    return static_cast<uint32_t>(AGUModule.headValue());
  };
  LQModule.agu.sqReplyValid = [this]() {
    return static_cast<uint32_t>(SQModule.reply.valid);
  };
  LQModule.agu.sqReplyValue = [this]() {
    return static_cast<uint32_t>(SQModule.reply.value);
  };
  LQModule.rob.isROBEmpty = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.isEmpty);
  };
  LQModule.rob.robHeadTag = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.head);
  };
  LQModule.rob.squashLQTailSnapshot = [this]() {
    return static_cast<uint32_t>(
        ROBModule.entry
            .lqTailSnapshot[static_cast<uint32_t>(flushArbiter.SquashTag) &
                            0x3F]);
  };
  // loadResp now comes from the DCache (hit self-answer or fill serve); the
  // squash guard lives inside DCache::wire_output (valid && (!needSquash ||
  // isOlder)).
  LQModule.loadResp.loadRespValid = [this]() {
    return static_cast<uint32_t>(DCacheModule.loadRespValid);
  };
  LQModule.loadResp.loadRespMemIndex = [this]() {
    return static_cast<uint32_t>(DCacheModule.loadRespMemIndex);
  };
  LQModule.loadResp.loadRespRobTag = [this]() {
    return static_cast<uint32_t>(DCacheModule.loadRespRobTag);
  };
  LQModule.loadResp.loadRespValue = [this]() {
    return static_cast<uint32_t>(DCacheModule.loadRespValue);
  };
  LQModule.memDispatch.memDispatchValid = [this]() {
    return MemArbiterModule.valid ? 1u : 0u;
  };
  LQModule.memDispatch.memDispatchIsLoad = [this]() {
    return static_cast<bool>(MemArbiterModule.valid) &&
           static_cast<uint32_t>(MemArbiterModule.op) ==
               static_cast<uint32_t>(Operation::Load);
  };
  LQModule.memDispatch.memDispatchMemIndex = [this]() {
    return static_cast<uint32_t>(MemArbiterModule.memIndex);
  };
  for (int i = 0; i < STORERS_CAP; ++i) {
    LQModule.storeNotifies.snValid[i] = [this, i]() {
      return static_cast<uint32_t>(SQModule.data.valid[i]);
    };
    LQModule.storeNotifies.snStoreTag[i] = [this, i]() {
      return static_cast<uint32_t>(SQModule.data.storeTag[i]);
    };
    LQModule.storeNotifies.snAddr[i] = [this, i]() {
      return static_cast<uint32_t>(SQModule.data.addr[i]);
    };
    LQModule.storeNotifies.snValue[i] = [this, i]() {
      return static_cast<uint32_t>(SQModule.data.value[i]);
    };
    LQModule.storeNotifies.snFoundKnownSame[i] = [this, i]() {
      return static_cast<uint32_t>(SQModule.data.foundKnownSame[i]);
    };
    LQModule.storeNotifies.snKnownTag[i] = [this, i]() {
      return static_cast<uint32_t>(SQModule.data.knownTag[i]);
    };
    LQModule.storeNotifies.snFoundUnknown[i] = [this, i]() {
      return static_cast<uint32_t>(SQModule.data.foundUnknown[i]);
    };
    LQModule.storeNotifies.snUnknownTag[i] = [this, i]() {
      return static_cast<uint32_t>(SQModule.data.unknownTag[i]);
    };
  }
  LQModule.storeNotifies.sanValid = [this]() {
    return static_cast<uint32_t>(SQModule.addr.valid);
  };
  LQModule.storeNotifies.sanStoreTag = [this]() {
    return static_cast<uint32_t>(SQModule.addr.storeTag);
  };
  LQModule.storeNotifies.sanAddr = [this]() {
    return static_cast<uint32_t>(SQModule.addr.addr);
  };
  LQModule.storeNotifies.sanValue = [this]() {
    return static_cast<uint32_t>(SQModule.addr.value);
  };
  LQModule.storeNotifies.sanFoundKnownSame = [this]() {
    return static_cast<uint32_t>(SQModule.addr.foundKnownSame);
  };
  LQModule.storeNotifies.sanKnownTag = [this]() {
    return static_cast<uint32_t>(SQModule.addr.knownTag);
  };
  LQModule.storeNotifies.sanFoundUnknown = [this]() {
    return static_cast<uint32_t>(SQModule.addr.foundUnknown);
  };
  LQModule.storeNotifies.sanUnknownTag = [this]() {
    return static_cast<uint32_t>(SQModule.addr.unknownTag);
  };

  // ---- Wire SQ's Input Wires ----
  SQModule.squash.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  SQModule.squash.SquashTag = [this]() {
    return static_cast<uint32_t>(flushArbiter.SquashTag);
  };
  SQModule.issue.issueValid = [this]() {
    return (static_cast<bool>(IssueArbiterModule.core.valid) &&
            static_cast<bool>(IssueArbiterModule.core.isStore))
               ? 1u
               : 0u;
  };
  SQModule.issue.issueStore = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.isStore);
  };
  SQModule.issue.issueTag = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.robTag);
  };
  SQModule.issue.issueBytes = [this]() {
    const uint32_t n = static_cast<uint32_t>(IssueArbiterModule.core.nBytes);
    return (n == 1 ? 0u : (n == 2 ? 1u : 2u));
  };
  SQModule.memDispatch.memDispatchValid = [this]() {
    return MemArbiterModule.valid ? 1u : 0u;
  };
  SQModule.memDispatch.memDispatchIsStore = [this]() {
    return static_cast<bool>(MemArbiterModule.valid) &&
           static_cast<uint32_t>(MemArbiterModule.op) ==
               static_cast<uint32_t>(Operation::Store);
  };
  SQModule.rob.squashSQTailSnapshot = [this]() {
    return static_cast<uint32_t>(
        ROBModule.entry
            .sqTailSnapshot[static_cast<uint32_t>(flushArbiter.SquashTag) &
                            0x3F]);
  };
  SQModule.agu.isAGUEmpty = [this]() { return AGUModule.isEmpty(); };
  SQModule.agu.aguHeadMemIndex = [this]() { return AGUModule.headMemIndex(); };
  SQModule.agu.aguHeadRobTag = [this]() { return AGUModule.headRobTag(); };
  SQModule.agu.aguHeadValue = [this]() {
    return static_cast<uint32_t>(AGUModule.headValue());
  };
  for (int i = 0; i < STORERS_CAP; ++i) {
    SQModule.prf.svWriteValid[i] = [this, i]() {
      if (RSModule.isSvFree(i) ||
          !PRFModule.isOperandReady(RSModule.getSvData(i)))
        return 0u;
      if (static_cast<bool>(flushArbiter.needSquash) &&
          !ROB::isOlder(RSModule.getSvRobTag(i),
                        static_cast<uint32_t>(flushArbiter.SquashTag)))
        return 0u;
      return 1u;
    };
    SQModule.prf.svValue[i] = [this, i]() {
      return static_cast<uint32_t>(
          PRFModule.getOperandValue(RSModule.getSvData(i)));
    };
    SQModule.prf.svMemIndex[i] = [this, i]() {
      return static_cast<uint32_t>(RSModule.getSvMemIndex(i));
    };
  }

  // ---- Wire ROB's Input Wires (already-converted consumers feed Output Wire
  // view) ----
  ROBModule.squash.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash) ? 1u : 0u;
  };
  ROBModule.squash.SquashTag = [this]() {
    return static_cast<uint32_t>(static_cast<uint32_t>(flushArbiter.SquashTag));
  };
  ROBModule.cdbOfALU.cdbValid = [this]() {
    return AluCDBArbiterModule.valid ? 1u : 0u;
  };
  ROBModule.cdbOfALU.cdbRobTag = [this]() {
    return static_cast<uint32_t>(AluCDBArbiterModule.robTag);
  };
  ROBModule.cdbOfLQ.cdbValid = [this]() {
    return LqCDBArbiterModule.valid ? 1u : 0u;
  };
  ROBModule.cdbOfLQ.cdbRobTag = [this]() {
    return static_cast<uint32_t>(LqCDBArbiterModule.robTag);
  };
  ROBModule.cdbOfMUL.cdbValid = [this]() {
    return MulCDBModule.valid ? 1u : 0u;
  };
  ROBModule.cdbOfMUL.cdbRobTag = [this]() {
    return static_cast<uint32_t>(MulCDBModule.robTag);
  };
  ROBModule.bru.isBRUEmpty = [this]() { return BRUModule.isEmpty() ? 1u : 0u; };
  ROBModule.bru.bruHeadRobTag = [this]() {
    return BRUModule.isEmpty() ? 0u
                               : static_cast<uint32_t>(BRUModule.headRobTag());
  };
  for (int i = 0; i < SQ_CAP; ++i) {
    ROBModule.sq.sqValid[i] = [this, i]() {
      return SQModule.isActive(i) ? 1u : 0u;
    };
    ROBModule.sq.sqReadyToCommit[i] = [this, i]() {
      return SQModule.isReadyToCommit(i) ? 1u : 0u;
    };
    ROBModule.sq.sqRobTag[i] = [this, i]() {
      return static_cast<uint32_t>(SQModule.getRobTag(i));
    };
  }
  ROBModule.sq.sqHead = [this]() {
    return static_cast<uint32_t>(SQModule.getHead());
  };
  for (int t = 0; t < 128; ++t) {
    ROBModule.sq.sqHasOlderUnresolvedAddressStore[t] = [this, t]() {
      return SQModule.hasOlderUnresolvedAddressStore(static_cast<uint8_t>(t))
                 ? 1u
                 : 0u;
    };
  }
  ROBModule.issue.issueValid = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.core.valid);
  };
  ROBModule.issue.entry.type = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.type);
  };
  ROBModule.issue.entry.isCommitReady = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.isCommitReady);
  };
  ROBModule.issue.entry.dest = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.dest);
  };
  ROBModule.issue.entry.halt = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.halt);
  };
  ROBModule.issue.entry.isCall = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.isCall);
  };
  ROBModule.issue.entry.isRet = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.isRet);
  };
  ROBModule.issue.entry.isIndirect = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.isIndirect);
  };
  ROBModule.issue.entry.ckptId = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.ckptId);
  };
  ROBModule.issue.entry.predictedPC = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.predictedPC);
  };
  ROBModule.issue.entry.pc = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.pc);
  };
  ROBModule.issue.entry.lqTailSnapshot = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.lqTailSnapshot);
  };
  ROBModule.issue.entry.sqTailSnapshot = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.sqTailSnapshot);
  };
  ROBModule.issue.entry.newPhy = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.newPhy);
  };
  ROBModule.issue.entry.oldPhy = [this]() {
    return static_cast<uint32_t>(IssueArbiterModule.robEntry.oldPhy);
  };

  // ---- Wire FlushArbiter's Input Wires (detection lives here, reads
  // bridge/_M_old) ----
  flushArbiter.squash.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash) ? 1u : 0u;
  };
  flushArbiter.squash.SquashTag = [this]() {
    return static_cast<uint32_t>(static_cast<uint32_t>(flushArbiter.SquashTag));
  };
  flushArbiter.bru.isBRUEmpty = [this]() {
    return BRUModule.isEmpty() ? 1u : 0u;
  };
  flushArbiter.bru.bruHeadRobTag = [this]() {
    return static_cast<uint32_t>(BRUModule.headRobTag());
  };
  flushArbiter.bru.bruHeadPCResult = [this]() {
    return static_cast<uint32_t>(BRUModule.headPCResult());
  };
  flushArbiter.bru.bruHeadPCFrom = [this]() {
    return static_cast<uint32_t>(BRUModule.headPCFrom());
  };
  // JALR mispredict detection consumes only the ALU bus (control results;
  // loads never produce control), so the LQ bus is not wired here -- an
  // area/port saving over a shared single CDB.
  flushArbiter.cdb.cdbValid = [this]() {
    return AluCDBArbiterModule.valid ? 1u : 0u;
  };
  flushArbiter.cdb.cdbValue = [this]() {
    return static_cast<uint32_t>(AluCDBArbiterModule.value);
  };
  flushArbiter.cdb.cdbRobTag = [this]() {
    return static_cast<uint32_t>(AluCDBArbiterModule.robTag);
  };
  flushArbiter.cdb.cdbIsControl = [this]() {
    return AluCDBArbiterModule.isControl ? 1u : 0u;
  };
  flushArbiter.rob.isROBEmpty = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.isEmpty);
  };
  flushArbiter.rob.robHeadTag = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.head);
  };
  for (int i = 0; i < ROB_CAP; ++i) {
    flushArbiter.rob.robPredictPC[i] = [this, i]() {
      return static_cast<uint32_t>(ROBModule.entry.predictedPC[i]);
    };
    flushArbiter.rob.robPC[i] = [this, i]() {
      return static_cast<uint32_t>(ROBModule.entry.pc[i]);
    };
    flushArbiter.rob.robCkptId[i] = [this, i]() {
      return static_cast<uint32_t>(ROBModule.entry.ckptId[i]);
    };
  }
  flushArbiter.agu.isAGUEmpty = [this]() {
    return AGUModule.isEmpty() ? 1u : 0u;
  };
  flushArbiter.agu.aguHeadValue = [this]() {
    return static_cast<uint32_t>(AGUModule.headValue());
  };
  flushArbiter.agu.aguHeadMemIndex = [this]() {
    return static_cast<uint32_t>(AGUModule.headMemIndex());
  };
  flushArbiter.agu.aguHeadRobTag = [this]() {
    return static_cast<uint32_t>(AGUModule.headRobTag());
  };
  flushArbiter.lq.lqHead = [this]() {
    return static_cast<uint32_t>(LQModule.getHead());
  };
  for (int i = 0; i < LQ_CAP; ++i) {
    flushArbiter.lq.lqActive[i] = [this, i]() {
      return LQModule.isActive(i) ? 1u : 0u;
    };
    flushArbiter.lq.lqAddressReady[i] = [this, i]() {
      return LQModule.isAddressReady(i) ? 1u : 0u;
    };
    flushArbiter.lq.lqRobTags[i] = [this, i]() {
      return static_cast<uint32_t>(LQModule.getRobTag(i));
    };
    flushArbiter.lq.lqAddress[i] = [this, i]() {
      return static_cast<uint32_t>(LQModule.getAddress(i));
    };
    flushArbiter.lq.lqValueState[i] = [this, i]() {
      return LQModule.getValueState(i);
    };
  }

  // ---- Wire BPU's Input Wires (BRU=EX/投机口, CDB=commit/表口) ----
  BPUModule.squash.needSquash = [this]() {
    return static_cast<bool>(flushArbiter.needSquash) ? 1u : 0u;
  };
  BPUModule.squash.SquashTag = [this]() {
    return static_cast<uint32_t>(static_cast<uint32_t>(flushArbiter.SquashTag));
  };
  BPUModule.squash.SquashCkpt = [this]() {
    return static_cast<uint32_t>(static_cast<uint32_t>(flushArbiter.CkptId));
  };
  // BPU training consumes only the ALU bus (its cdb port gates on
  // cdbValid && cdbIsControl; loads never produce control), so the LQ bus is
  // not wired here -- an area/port saving over a shared single CDB.
  BPUModule.cdb.cdbValid = [this]() {
    return AluCDBArbiterModule.valid ? 1u : 0u;
  };
  BPUModule.cdb.cdbValue = [this]() {
    return static_cast<uint32_t>(AluCDBArbiterModule.value);
  };
  BPUModule.cdb.cdbRobTag = [this]() {
    return static_cast<uint32_t>(AluCDBArbiterModule.robTag);
  };
  BPUModule.cdb.cdbIsControl = [this]() {
    return AluCDBArbiterModule.isControl ? 1u : 0u;
  };
  BPUModule.bru.isBRUEmpty = [this]() { return BRUModule.isEmpty() ? 1u : 0u; };
  BPUModule.bru.bruHeadRobTag = [this]() {
    return static_cast<uint32_t>(BRUModule.headRobTag());
  };
  BPUModule.bru.bruHeadPCResult = [this]() {
    return static_cast<uint32_t>(BRUModule.headPCResult());
  };
  BPUModule.bru.bruHeadPCFrom = [this]() {
    return static_cast<uint32_t>(BRUModule.headPCFrom());
  };
  BPUModule.rob.isROBEmpty = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.isEmpty);
  };
  BPUModule.rob.robHeadTag = [this]() {
    return static_cast<uint32_t>(ROBModule.headView.head);
  };
  for (int i = 0; i < ROB_CAP; ++i) {
    BPUModule.rob.robPredictPC[i] = [this, i]() {
      return static_cast<uint32_t>(ROBModule.entry.predictedPC[i]);
    };
    BPUModule.rob.robPC[i] = [this, i]() {
      return static_cast<uint32_t>(ROBModule.entry.pc[i]);
    };
    BPUModule.rob.robIsCall[i] = [this, i]() {
      return static_cast<uint32_t>(ROBModule.entry.isCall[i]);
    };
    BPUModule.rob.robIsRet[i] = [this, i]() {
      return static_cast<uint32_t>(ROBModule.entry.isRet[i]);
    };
    BPUModule.rob.robCkptId[i] = [this, i]() {
      return static_cast<uint32_t>(ROBModule.entry.ckptId[i]);
    };
  }
  // Fetch-context ports: the prediction bundle is produced inside the BPU
  // (wire_output() calls this->predict, single evaluation point), so only
  // the fetch context needs wiring here.
  BPUModule.fetchCtx.pc = [this]() {
    return static_cast<uint32_t>(FetchUnitModule.programCounter);
  };
  BPUModule.fetchCtx.squashNeed = [this]() {
    return static_cast<bool>(flushArbiter.needSquash);
  };
  BPUModule.fetchCtx.haltFetched = [this]() {
    return static_cast<bool>(FetchUnitModule.haltFetched);
  };
  BPUModule.fetchCtx.fqFull = [this]() { return FQModule.isFull(); };
  BPUModule.fetchCtx.imemReqFull = [this]() {
    return ICacheModule.isRequestFull() || IMEMModule.isRequestFull();
  };
  // fetchInfo: scan the FQ's registered last* (the push record of the
  // PREVIOUS cycle). Mirrors the main tree, which reads pushCache in comb()
  // (also the previous cycle's FQ.tick write). Do NOT use the ICache return
  // bundle directly -- it is the current-cycle value and races FQ push gating.
  BPUModule.fetchInfo.FetchValid = [this]() {
    return scanJump(FQModule.getLastValid(), FQModule.getLastRaw(),
                    FQModule.getLastPC())
                   .valid
               ? 1u
               : 0u;
  };
  BPUModule.fetchInfo.isFetchCall = [this]() {
    return scanJump(FQModule.getLastValid(), FQModule.getLastRaw(),
                    FQModule.getLastPC())
                   .isCall
               ? 1u
               : 0u;
  };
  BPUModule.fetchInfo.isFetchRet = [this]() {
    return scanJump(FQModule.getLastValid(), FQModule.getLastRaw(),
                    FQModule.getLastPC())
                   .isRet
               ? 1u
               : 0u;
  };
  BPUModule.fetchInfo.FetchJALTargetValid = [this]() {
    return scanJump(FQModule.getLastValid(), FQModule.getLastRaw(),
                    FQModule.getLastPC())
                   .jalTargetValid
               ? 1u
               : 0u;
  };
  BPUModule.fetchInfo.FetchPC = [this]() {
    return scanJump(FQModule.getLastValid(), FQModule.getLastRaw(),
                    FQModule.getLastPC())
        .pc;
  };
  BPUModule.fetchInfo.FetchJALTarget = [this]() {
    return scanJump(FQModule.getLastValid(), FQModule.getLastRaw(),
                    FQModule.getLastPC())
        .jalTarget;
  };

  // ---- Wire SQ's Output Wires (notify/reply production, single point) ----
  for (int i = 0; i < STORERS_CAP; ++i) {
    SQModule.data.valid[i] = [this, i]() {
      if (RSModule.isSvFree(i) ||
          !PRFModule.isOperandReady(RSModule.getSvData(i)))
        return 0u;
      if (static_cast<bool>(flushArbiter.needSquash) &&
          !ROB::isOlder(RSModule.getSvRobTag(i),
                        static_cast<uint32_t>(flushArbiter.SquashTag)))
        return 0u;
      return SQModule.planDataForward(
                         memSlot(RSModule.getSvMemIndex(i)),
                         PRFModule.getOperandValue(RSModule.getSvData(i)))
                     .valid
                 ? 1u
                 : 0u;
    };
    SQModule.data.storeTag[i] = [this, i]() {
      return SQModule
          .planDataForward(memSlot(RSModule.getSvMemIndex(i)),
                           PRFModule.getOperandValue(RSModule.getSvData(i)))
          .storeTag;
    };
    SQModule.data.addr[i] = [this, i]() {
      return SQModule
          .planDataForward(memSlot(RSModule.getSvMemIndex(i)),
                           PRFModule.getOperandValue(RSModule.getSvData(i)))
          .addr;
    };
    SQModule.data.value[i] = [this, i]() {
      return static_cast<uint32_t>(
          SQModule
              .planDataForward(memSlot(RSModule.getSvMemIndex(i)),
                               PRFModule.getOperandValue(RSModule.getSvData(i)))
              .value);
    };
    SQModule.data.foundKnownSame[i] = [this, i]() {
      return SQModule.planDataForward(
                         memSlot(RSModule.getSvMemIndex(i)),
                         PRFModule.getOperandValue(RSModule.getSvData(i)))
                     .foundKnownSame
                 ? 1u
                 : 0u;
    };
    SQModule.data.knownTag[i] = [this, i]() {
      return SQModule
          .planDataForward(memSlot(RSModule.getSvMemIndex(i)),
                           PRFModule.getOperandValue(RSModule.getSvData(i)))
          .knownSameAddressOldestTag;
    };
    SQModule.data.foundUnknown[i] = [this, i]() {
      return SQModule.planDataForward(
                         memSlot(RSModule.getSvMemIndex(i)),
                         PRFModule.getOperandValue(RSModule.getSvData(i)))
                     .foundUnknown
                 ? 1u
                 : 0u;
    };
    SQModule.data.unknownTag[i] = [this, i]() {
      return SQModule
          .planDataForward(memSlot(RSModule.getSvMemIndex(i)),
                           PRFModule.getOperandValue(RSModule.getSvData(i)))
          .unknownOldestTag;
    };
  }
  SQModule.addr.valid = [this]() {
    if (AGUModule.isEmpty() || !isStoreMem(AGUModule.headMemIndex()))
      return 0u;
    if (static_cast<bool>(flushArbiter.needSquash) &&
        !ROB::isOlder(AGUModule.headRobTag(),
                      static_cast<uint32_t>(flushArbiter.SquashTag)))
      return 0u;
    return SQModule.planAddressForward(
                       memSlot(AGUModule.headMemIndex()),
                       static_cast<uint32_t>(AGUModule.headValue()))
                   .valid
               ? 1u
               : 0u;
  };
  SQModule.addr.storeTag = [this]() {
    return SQModule
        .planAddressForward(memSlot(AGUModule.headMemIndex()),
                            static_cast<uint32_t>(AGUModule.headValue()))
        .storeTag;
  };
  SQModule.addr.addr = [this]() {
    return SQModule
        .planAddressForward(memSlot(AGUModule.headMemIndex()),
                            static_cast<uint32_t>(AGUModule.headValue()))
        .addr;
  };
  SQModule.addr.value = [this]() {
    return static_cast<uint32_t>(
        SQModule
            .planAddressForward(memSlot(AGUModule.headMemIndex()),
                                static_cast<uint32_t>(AGUModule.headValue()))
            .value);
  };
  SQModule.addr.foundKnownSame = [this]() {
    return SQModule.planAddressForward(
                       memSlot(AGUModule.headMemIndex()),
                       static_cast<uint32_t>(AGUModule.headValue()))
                   .foundKnownSame
               ? 1u
               : 0u;
  };
  SQModule.addr.knownTag = [this]() {
    return SQModule
        .planAddressForward(memSlot(AGUModule.headMemIndex()),
                            static_cast<uint32_t>(AGUModule.headValue()))
        .knownSameAddressOldestTag;
  };
  SQModule.addr.foundUnknown = [this]() {
    return SQModule.planAddressForward(
                       memSlot(AGUModule.headMemIndex()),
                       static_cast<uint32_t>(AGUModule.headValue()))
                   .foundUnknown
               ? 1u
               : 0u;
  };
  SQModule.addr.unknownTag = [this]() {
    return SQModule
        .planAddressForward(memSlot(AGUModule.headMemIndex()),
                            static_cast<uint32_t>(AGUModule.headValue()))
        .unknownOldestTag;
  };
  SQModule.reply.valid = [this]() {
    if (AGUModule.isEmpty() || isStoreMem(AGUModule.headMemIndex()))
      return 0u;
    auto r = SQModule.replyToLoadRequest(
        static_cast<uint32_t>(AGUModule.headValue()), AGUModule.headRobTag());
    return r.valid ? 1u : 0u;
  };
  SQModule.reply.value = [this]() {
    auto r = SQModule.replyToLoadRequest(
        static_cast<uint32_t>(AGUModule.headValue()), AGUModule.headRobTag());
    return r.valid ? static_cast<uint32_t>(r.value) : 0u;
  };
}

void CPU::run(bool shuffle) {
  bool finish = false;
  while (!finish) {
    // Snapshot semantics (mirrors main tree): finish samples the state as of
    // the START of this cycle, not the post-sync state -- the sampling MUST
    // precede run_once(), which advances and syncs the whole module set.
    bool s_halt = ROBModule.isHaltCommitted();
    bool s_fqEmpty = FQModule.isEmpty();
    bool s_decEmpty = DecodeUnitModule.isEmpty();
    bool s_robEmpty = ROBModule.isEmpty();
    bool s_sqEmpty = SQModule.isEmpty();
    bool s_dmemFree = !DMEMModule.isReadBusy() && !DMEMModule.isWriteBusy();
    bool s_dcacheFree = !DCacheModule.isBusy();
    if (shuffle)
      dcpu.run_once_shuffle();
    else
      dcpu.run_once();
    // Halt drain (mirrors main tree): stop only after every committed store
    // has left the SQ and reached the DCache (SQ empty), and after any
    // in-flight cache refill/writeback finished (DCache idle + DMEM both
    // ports idle). Otherwise a store committed right before halt could be
    // truncated while its fill spans the halt window.
    finish = s_halt && s_fqEmpty && s_decEmpty && s_robEmpty &&
             s_sqEmpty && s_dmemFree && s_dcacheFree;
  }
  if (debug::enabled(debug::TOPIC_DCACHE))
    debug::print("dcache: hits=%llu misses=%llu total=%llu hit-rate=%.2f%% "
                 "writebacks=%llu\n",
                 DCacheModule.statHits, DCacheModule.statMisses,
                 DCacheModule.statHits + DCacheModule.statMisses,
                 (DCacheModule.statHits + DCacheModule.statMisses)
                     ? 100.0 * DCacheModule.statHits /
                           (DCacheModule.statHits + DCacheModule.statMisses)
                     : 0.0,
                 DCacheModule.statWritebacks);
  if (debug::enabled(debug::TOPIC_CLOCK))
    debug::print("clock: %llu\n", dcpu.cycles);
  if (debug::enabled(debug::TOPIC_BRANCH))
    debug::print("branch: %llu/%llu correct (%.2f%%)\n",
                 BPUModule.getBranchCorrect(), BPUModule.getBranchTotal(),
                 BPUModule.getBranchTotal()
                     ? 100.0 * BPUModule.getBranchCorrect() /
                           BPUModule.getBranchTotal()
                     : 0.0);
  std::cout << std::dec
            << (PRFModule.getValue(
                    RATModule.readRAT_PRF(ROBModule.getHaltRd())) &
                0xFF)
            << std::endl;
}
