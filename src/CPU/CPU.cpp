#include "../include/CPU.hpp"
#include "../include/util.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {
// Pre-decode scan of a fetched word: classify the unconditional-jump family
// (jal / jalr) so RAS maintenance and BTB type training no longer depend on
// prediction-table hits. Conditional branches are deliberately excluded.
FetchTypeInfo scanJump(const struct lastPush &lp) {
  FetchTypeInfo fi{};
  if (!lp.valid)
    return fi;
  const uint32_t raw = lp.raw_inst;
  const uint32_t opcode = raw & 0x7F;
  const uint32_t rd = (raw >> 7) & 0x1F;
  const uint32_t rs1 = (raw >> 15) & 0x1F;
  const uint32_t funct3 = (raw >> 12) & 0x7;
  // RISC-V RAS hints (unpriv spec 2.5): link registers are x1/ra and x5/t0.
  const bool rdLink = (rd == 1 || rd == 5);
  const bool rs1Link = (rs1 == 1 || rs1 == 5);
  fi.pc = lp.pc;
  if (opcode == 0x6F) { // jal: direct call iff rd is a link register
    fi.isCall = rdLink;
    fi.jalTargetValid = true;
    uint32_t uoff = ((raw >> 31) & 1U) << 20;         // imm[20]
    uoff |= ((raw >> 20) & 1U) << 11;                 // imm[11]
    for (int i = 12; i <= 19; ++i)                    // imm[19:12]
      uoff |= ((raw >> i) & 1U) << i;
    for (int i = 21; i <= 30; ++i)                    // imm[10:1]
      uoff |= ((raw >> i) & 1U) << (i - 20);
    const auto off =
        static_cast<int32_t>((uoff ^ 0x100000U) - 0x100000U); // sign-extend
    fi.jalTarget = static_cast<uint32_t>(static_cast<int32_t>(lp.pc) + off);
    fi.valid = true;
  } else if (opcode == 0x67 && funct3 == 0) { // jalr
    // Indirect call: rd is a link register (push ra), even for non-link rs1
    // (function-pointer / PLT / vtable calls).
    // Return: rs1 is a link register and rd is NOT (pop).
    // Corner case rd-link & rs1-link (coroutine pop+push) folds to push-only
    // here -- vanishingly rare in compiler output, acceptable for v1.
    fi.isCall = rdLink;
    fi.isRet = rs1Link && !rdLink;
    fi.valid = true;
  }
  return fi;
}
} // namespace

CPU::CPU(Memory mem)
    : CPUstate(mem), InstructMem(mem), IMEMModule(mem), DMEMModule(mem) {}

void CPU::comb() {
  memcpy(&RSModule, &CPUstate.RSModule, sizeof(RSModule));
  memcpy(&RATModule, &CPUstate.RATModule, sizeof(RATModule));
  memcpy(&ROBModule, &CPUstate.ROBModule, sizeof(ROBModule));
  memcpy(&ALUModule, &CPUstate.ALUModule, sizeof(ALUModule));
  memcpy(&MULModule, &CPUstate.MULModule, sizeof(MULModule));
  memcpy(&DIVModule, &CPUstate.DIVModule, sizeof(DIVModule));
  memcpy(&AGUModule, &CPUstate.AGUModule, sizeof(AGUModule));
  memcpy(&BRUModule, &CPUstate.BRUModule, sizeof(BRUModule));
  memcpy(&LQModule, &CPUstate.LQModule, sizeof(LQModule));
  memcpy(&SQModule, &CPUstate.SQModule, sizeof(SQModule));
  memcpy(&FQModule, &CPUstate.FQModule, sizeof(FQModule));
  memcpy(&ICacheModule, &CPUstate.ICacheModule, sizeof(ICacheModule));
  memcpy(&DecodeUnitModule, &CPUstate.DecodeUnitModule,
         sizeof(DecodeUnitModule));
  memcpy(&PRFModule, &CPUstate.PRFModule, sizeof(PRFModule));
  memcpy(&BPUModule, &CPUstate.BPUModule, sizeof(BPUModule));
  IMEMModule.snapshotFrom(CPUstate.IMEMModule);
  memcpy(&flushArbiter, &CPUstate.flushArbiter, sizeof(flushArbiter));
  memcpy(&FetchUnitModule, &CPUstate.FetchUnitModule,
         sizeof(FetchUnitModule));
  DMEMModule.snapshotFrom(CPUstate.DMEMModule);
  DCacheModule.snapshotFrom(CPUstate.DCacheModule);
  squashDetect = CPUstate.flushArbiter.arbitResult();
  fetchDecision = FetchDecision::build(
      BPUModule, FetchUnitModule.getPC(), squashDetect,
      FetchUnitModule.isHaltFetched(), FQModule.isFull(),
      ICacheModule.isRequestFull() || IMEMModule.isRequestFull());
  // FetchUnit halt signal: latch when the ICache head holds the halt
  // instruction (combinational bus)
  {
    bool haltSignal =
        ICacheModule.isReturnReady() && ICacheModule.returnRaw() == 0x0ff00513;
    fetchUnitInput.squashDetect = squashDetect;
    fetchUnitInput.fetchDecision = fetchDecision;
    fetchUnitInput.haltSignal = haltSignal;
  }
  // ICache hit check (comb, read snapshot ICacheModule) -> gate IMEM miss request
  // The line-return and FQ-consume handshakes are combinational predicates,
  // each side clears its own state (write-own-only)
  {
    bool icacheHit = fetchDecision.valid && ICacheModule.hit(fetchDecision.pc);
    FetchDecision imemFetch = fetchDecision;
    if (icacheHit)
      imemFetch.valid = false; // hit: no IMEM line fetch needed
    else if (fetchDecision.valid)
      imemFetch.pc = fetchDecision.pc & ~0xF; // line-aligned block addr for IMEM
    LineReturn lineReturn = IMEMModule.getReturn();
    imemInput.squashDetect = squashDetect;
    imemInput.fetchDecision = imemFetch;
    imemInput.lineConsumed = lineReturn.valid;
    icacheInput.squashDetect = squashDetect;
    icacheInput.fetchDecision = fetchDecision;
    icacheInput.lineReturn = lineReturn;
    bool popConsume = ICacheModule.isReturnReady() &&
                      !FetchUnitModule.isHaltFetched() && !FQModule.isFull();
    icacheInput.popConsume = popConsume;
  }
  fqInput.squashDetect = squashDetect;
  fqInput.haltFetched = FetchUnitModule.isHaltFetched();
  bpuInput.fetchDecision = fetchDecision;
  cdbOfALU = aluCDB::build(ALUModule, squashDetect);
  cdbOfLQ = lqCDB::build(LQModule, squashDetect);
  cdbOfMul = mulCDB::build(MULModule, squashDetect);
  cdbOfDiv = divCDB::build(DIVModule, squashDetect);
  // dual-CDB contention stats: count cycles where both buses have a grant,
  // and which side a single-CDB arbiter would have preferred (older tag).
  if (cdbOfALU.valid && cdbOfLQ.valid) {
    ++statBoth;
    if (ROB::isOlder(cdbOfLQ.robTag, cdbOfALU.robTag))
      ++statLqWins;
    else
      ++statAluWins;
  } else if (cdbOfALU.valid) {
    ++statAluOnly;
  } else if (cdbOfLQ.valid) {
    ++statLqOnly;
  }
  DispatchBus dispatchBus = DispatchArbiter::arbitrate(
      RSModule, ALUModule, AGUModule, BRUModule, MULModule, DIVModule,
      ROBModule, PRFModule,
      squashDetect);
  aguInput.squashDetect = squashDetect;
  aluInput.squashDetect = squashDetect;
  aluInput.cdbOutput = cdbOfALU;
  aluInput.dispatch = dispatchBus.alu;
  mulInput.squashDetect = squashDetect;
  mulInput.cdbOutput = cdbOfMul;
  mulInput.dispatch = dispatchBus.mul;
  divInput.squashDetect = squashDetect;
  divInput.cdbOutput = cdbOfDiv;
  divInput.dispatch = dispatchBus.div;
  aguInput.dispatch = dispatchBus.agu;
  bruInput.dispatch = dispatchBus.bru;
  rsInput.dispatchBus = dispatchBus;
  decodeInput.squashDetect = squashDetect;
  lqInput.squashDetect = squashDetect;
  sqInput.squashDetect = squashDetect;
  auto memDispatch = MemArbiter::arbitrate(LQModule, SQModule, ROBModule,
                                           DCacheModule, squashDetect);
  dcacheInput.squashDetect = squashDetect;
  dcacheInput.decision = memDispatch;
  // DMEM now only ever receives requests forwarded by the DCache: the DCache
  // emits its dual-channel pulse (registered in the live module at the end of
  // the previous tick, mirrored into the snapshot by snapshotFrom).
  dmemInput.request = DCacheModule.forwardRequest();
  lqInput.decision = memDispatch;
  sqInput.decision = memDispatch;
  // store value-ready broadcast (data event): scan ready storeValueRS entries
  // against the SQ snapshot (store address must already be known)
  for (int i = 0; i < STORERS_CAP; ++i) {
    lqInput.storeNotifies[i] = StoreNotify{};
    if (!RSModule.storeValueRS[i].free &&
        PRFModule.isOperandReady(RSModule.storeValueRS[i].data)) {
      auto tag = RSModule.storeValueRS[i].robTag;
      if (!squashDetect.needSquash ||
          (squashDetect.needSquash &&
           ROB::isOlder(tag, squashDetect.SquashTag))) {
        lqInput.storeNotifies[i] = SQModule.planDataForward(
            memSlot(RSModule.storeValueRS[i].memIndex),
            PRFModule.getOperandValue(RSModule.storeValueRS[i].data));
      }
    }
  }
  // store address-ready broadcast (address event): AGU head is a store
  lqInput.storeAddrNotify = StoreNotify{};
  if (!AGUModule.isEmpty() && isStoreMem(AGUModule.headMemIndex())) {
    auto aguRobTag = AGUModule.headRobTag();
    if (!squashDetect.needSquash ||
        (squashDetect.needSquash &&
         ROB::isOlder(aguRobTag, squashDetect.SquashTag))) {
      lqInput.storeAddrNotify = SQModule.planAddressForward(
          memSlot(AGUModule.headMemIndex()),
          static_cast<uint32_t>(AGUModule.headValue()));
    }
  }
  rsInput.squashDetect = squashDetect;
  robInput.squashDetect = squashDetect;
  robInput.cdbOfALU = cdbOfALU;
  robInput.cdbOfLQ = cdbOfLQ;
  robInput.cdbOfMul = cdbOfMul;
  robInput.cdbOfDiv = cdbOfDiv;
  prfInput.squashDetect = squashDetect;
  prfInput.cdbOfALU = cdbOfALU;
  prfInput.cdbOfLQ = cdbOfLQ;
  prfInput.cdbOfMul = cdbOfMul;
  prfInput.cdbOfDiv = cdbOfDiv;
  ratInput.squashDetect = squashDetect;
  flarbInput.squashDetect = squashDetect;
  flarbInput.cdbOut = cdbOfALU;
  isarbInput.squashDetect = squashDetect;
  issuePacket = IssueArbiter::build(isarbInput);
  bruInput.squashDetect = squashDetect;
  bpuInput.squashDetect = squashDetect;
  bpuInput.cdbOut = cdbOfALU;
  bpuInput.fetchInfo = scanJump(FQModule.getLastPush());
  lqInput.cdbOutput = cdbOfLQ;
  // All load responses now come from the DCache (it is DMEM's sole client;
  // DMEM never receives Operation::Load anymore).
  lqInput.loadResp = DCacheModule.loadResp(squashDetect);
}

void CPU::run() {
  bool finish = false;
  uint64_t clock = 0;
  while (!finish) {
    comb();
    IMEMModule.tick(imemInput, CPUstate);
    FetchUnitModule.tick(fetchUnitInput, CPUstate);
    ICacheModule.tick(icacheInput, CPUstate);
    FQModule.tick(fqInput, CPUstate);
    LQModule.tick(lqInput, CPUstate);
    SQModule.tick(sqInput, CPUstate);
    ROBModule.tick(robInput, CPUstate);
    PRFModule.tick(prfInput, CPUstate);
    ALUModule.tick(aluInput, CPUstate);
    MULModule.tick(mulInput, CPUstate);
    DIVModule.tick(divInput, CPUstate);
    AGUModule.tick(aguInput, CPUstate);
    BRUModule.tick(bruInput, CPUstate);
    BPUModule.tick(bpuInput, CPUstate);
    DMEMModule.tick(dmemInput, CPUstate);
    DCacheModule.tick(dcacheInput, CPUstate);
    RSModule.tick(rsInput, CPUstate);
    RATModule.tick(ratInput, CPUstate);
    flushArbiter.tick(flarbInput, CPUstate);
    DecodeUnitModule.tick(decodeInput, CPUstate);
    ++clock;
    // Halt drain: once halt has committed and the pipeline is empty, the
    // machine must still wait for every committed store to leave the SQ and
    // reach the DCache (SQ empty), and for any in-flight cache refill /
    // writeback to finish (DCache idle, DMEM both ports idle). Without this,
    // a store committed right before halt could be truncated when the tail
    // fills span the halt window (SQ/park store never lands).
    finish = ROBModule.isHaltCommitted() && FQModule.isEmpty() &&
             DecodeUnitModule.isEmpty() && ROBModule.isEmpty() &&
             SQModule.isEmpty() && !DCacheModule.isBusy() &&
             !DMEMModule.isReadBusy() && !DMEMModule.isWriteBusy();
  }
  if (debug::enabled(debug::TOPIC_CLOCK))
    debug::print("clock: %llu\n", clock);
  if (debug::enabled(debug::TOPIC_BRANCH)) {
    // host-only: everything below this block is an end-of-run report for the
    // human (double percentage math, stdio formatting). It is not part of the
    // modelled datapath, which is why `*` and `/` are legal here.
    // Summary line format is load-bearing: test.sh parses "branch: x/y
    // correct (p%)" ($2 = x/y, $4 = (p%)), so it MUST stay as-is. The
    // per-class breakdown is a separate line prefixed "branch-type:" which
    // test.sh's `^branch:` grep deliberately does not match.
    debug::print("branch: %llu/%llu correct (%.2f%%)\n",
                 CPUstate.BPUModule.getBranchCorrect(),
                 CPUstate.BPUModule.getBranchTotal(),
                 CPUstate.BPUModule.getBranchTotal()
                     ? 100.0 * CPUstate.BPUModule.getBranchCorrect() /
                           CPUstate.BPUModule.getBranchTotal()
                     : 0.0);
    const auto &bp = CPUstate.BPUModule;
    auto pct = [](uint64_t c, uint64_t t) {
      return t ? 100.0 * static_cast<double>(c) / static_cast<double>(t) : 0.0;
    };
    debug::print(
        "branch-type: cond=%llu/%llu(%.2f%%) jal=%llu/%llu(%.2f%%) "
        "jalr=%llu/%llu(%.2f%%)\n",
        bp.getCondCorrect(), bp.getCondTotal(),
        pct(bp.getCondCorrect(), bp.getCondTotal()), bp.getJalCorrect(),
        bp.getJalTotal(), pct(bp.getJalCorrect(), bp.getJalTotal()),
        bp.getJalrCorrect(), bp.getJalrTotal(),
        pct(bp.getJalrCorrect(), bp.getJalrTotal()));
  }
  if (debug::enabled(debug::TOPIC_BPMISS))
    CPUstate.BPUModule.dumpBpMiss();
  // host-only: same end-of-run report as the branch block above.
  if (debug::enabled(debug::TOPIC_ICACHE)) {
    uint32_t h = CPUstate.ICacheModule.getHitCount();
    uint32_t m = CPUstate.ICacheModule.getMissCount();
    uint32_t t = h + m;
    debug::print("icache: hits=%u misses=%u total=%u hit-rate=%.2f%%\n", h, m, t,
                 t ? 100.0 * h / t : 0.0);
    uint64_t dh = CPUstate.DCacheModule.getHitCount();
    uint64_t dm = CPUstate.DCacheModule.getMissCount();
    uint64_t dt = dh + dm;
    debug::print("dcache: hits=%llu misses=%llu total=%llu hit-rate=%.2f%%\n",
                 dh, dm, dt, dt ? 100.0 * static_cast<double>(dh) /
                                      static_cast<double>(dt)
                                : 0.0);
  }
  if (debug::enabled(debug::TOPIC_CDB)) {
    debug::print("cdb: both=%llu aluOnly=%llu lqOnly=%llu "
                 "(lqWins=%llu aluWins=%llu) total=%llu\n",
                 statBoth, statAluOnly, statLqOnly, statLqWins, statAluWins,
                 clock);
  }
  std::cout << std::dec
            << (PRFModule.getValue(
                    RATModule.readRAT_PRF(ROBModule.getHaltRd())) &
                 0xFF)
            << std::endl;
}