#include "../include/CPU.hpp"
#include "../include/util.hpp"
#include "common.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

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
    uint32_t uoff = ((raw >> 31) & 1U) << 20; // imm[20]
    uoff |= ((raw >> 20) & 1U) << 11;         // imm[11]
    for (int i = 12; i <= 19; ++i)            // imm[19:12]
      uoff |= ((raw >> i) & 1U) << i;
    for (int i = 21; i <= 30; ++i) // imm[10:1]
      uoff |= ((raw >> i) & 1U) << (i - 20);
    const auto off =
        static_cast<int32_t>((uoff ^ 0x100000U) - 0x100000U); // sign-extend
    // uint32 bit-vector add: signed int32_t add past the range is host UB.
    fi.jalTarget = static_cast<uint32_t>(lp.pc) + static_cast<uint32_t>(off);
    fi.valid = true;
  } else if (opcode == 0x67 && funct3 == 0) { // jalr
    // User RAS contract: link rd pushes; link rs1 pops, independently.
    // Both link operands (including rd == rs1) perform POP then PUSH.
    fi.isCall = rdLink;
    fi.isRet = rs1Link;
    fi.valid = true;
  }
  return fi;
}

} // namespace

CPU::CPU(const Memory &mem) : CPUstate(mem) {}

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
  AXIFetchModule = CPUstate.AXIFetchModule;
  AXIDataModule = CPUstate.AXIDataModule;
  AXIBusModule = CPUstate.AXIBusModule;
  SlaveModule.snapshotFrom(CPUstate.SlaveModule);
  memcpy(&flushArbiter, &CPUstate.flushArbiter, sizeof(flushArbiter));
  memcpy(&FetchUnitModule, &CPUstate.FetchUnitModule, sizeof(FetchUnitModule));
  initialized = CPUstate.initialized;
  exitCommitted = CPUstate.exitCommitted;
  recovery = CPUstate.recovery;
  // The SRAM-backed lookup reads this old snapshot, like ICache.
  memcpy(&DCacheModule, &CPUstate.DCacheModule, sizeof(DCacheModule));
  squashDetect = CPUstate.flushArbiter.arbitResult();
  if (recovery.active || exitCommitted) squashDetect.needSquash = false;
  squashDetect.pauseCommit = recovery.active || squashDetect.needSquash;
  auto commits = ROBModule.planCommit(squashDetect, SQModule);
  if (!initialized || exitCommitted) commits = {};
  robInput.commits = ratInput.commits = prfInput.commits = lqInput.commits =
      sqInput.commits = bpuInput.commits = commits;
  isarbInput.squashDetect = squashDetect;
  issuePacket = IssueArbiter::build(isarbInput);
  if (exitCommitted || recovery.active) issuePacket.valid = false;
  ratInput.recovery = recovery;
  prfInput.recovery = recovery;
  bpuInput.recovery = recovery;
  const auto replay = ROBModule.recoveryWindow(recovery.active ? recovery.offset : 0);
  ratInput.replay = prfInput.replay = replay.narrow;
  bpuInput.replay = replay.ras;
  lqInput.executeEnabled = !recovery.active;
  sqInput.executeEnabled = !recovery.active;
  exitRetiring = commits.exits;
  sqInput.retireEnabled = !exitCommitted;
  fqInput.issueValid = issuePacket.valid;
  const bool fqPop = !recovery.active && !squashDetect.needSquash && !FQModule.isEmpty() &&
                     DecodeUnitModule.canAccept(issuePacket.valid);
  const bool fqReady = !squashDetect.needSquash && FQModule.canAccept(fqPop);
  const bool popConsume = !recovery.active && fqReady && ICacheModule.isReturnReady() &&
                          !FetchUnitModule.isHaltFetched();
  const bool haltSignal =
      FetchUnitModule.isHaltFetched() ||
       (legacyHaltEnabled && popConsume && ICacheModule.returnRaw() == 0x0ff00513);
  // Build the accepted fetch before copying it into any consumer input.
  const LineReturn lineReturn = AXIFetchModule.getReturn();
  const auto refill = ICacheModule.selectRefill(lineReturn);
  const bool fillFire =
      lineReturn.valid && refill.valid && !squashDetect.needSquash;
  const uint32_t pc = FetchUnitModule.getPC();
  const bool icacheHit = ICacheModule.hit(pc);
  const bool refillHit = fillFire && ICache::refillHit(pc, lineReturn);
  const bool readBlocked = fillFire && icacheHit && !refillHit;
  const bool memCanAccept = (!ICacheModule.isRequestFull() || popConsume) &&
                             (!AXIFetchModule.isRequestFull() || refillHit) &&
                            !readBlocked;
  fetchDecision = FetchDecision::build(BPUModule, pc, squashDetect, haltSignal,
                                         fqReady, memCanAccept && initialized && !exitCommitted && !recovery.active);
  {
    fetchUnitInput.squashDetect = squashDetect;
    fetchUnitInput.fetchDecision = fetchDecision;
    fetchUnitInput.haltSignal = haltSignal;
  }
  // ICache hit check (comb, read snapshot ICacheModule) -> gate IMEM miss
  // request The line-return and FQ-consume handshakes are combinational
  // predicates, each side clears its own state (write-own-only)
  {
    FetchDecision imemFetch = fetchDecision;
    if (icacheHit || refillHit)
      imemFetch.valid = false; // hit: no IMEM line fetch needed
    else if (fetchDecision.valid)
      imemFetch.pc = fetchDecision.pc &
                     ~ICACHE_OFFSET_MASK; // line-aligned block addr for IMEM
    imemInput.squashDetect = squashDetect;
    imemInput.fetchDecision = imemFetch;
    imemInput.lineConsumed = fillFire;
    icacheInput.squashDetect = squashDetect;
    icacheInput.fetchDecision = fetchDecision;
    icacheInput.lineReturn = lineReturn;
    icacheInput.lineReturn.valid = fillFire;
    icacheInput.refillSlot = refill.slot;
    icacheInput.popConsume = popConsume;
  }
  fqInput.squashDetect = squashDetect;
  fqInput.returnAccepted = popConsume;
  bpuInput.fetchDecision = fetchDecision;
  cdbOfALU = aluCDB::build(ALUModule, squashDetect);
  cdbOfLQ = lqCDB::build(LQModule, squashDetect);
  cdbOfMul = mulCDB::build(MULModule, squashDetect);
  cdbOfDiv = divCDB::build(DIVModule, squashDetect);
  if (recovery.active) {
    cdbOfALU.valid = false; cdbOfLQ.valid = false;
    cdbOfMul.valid = false; cdbOfDiv.valid = false;
  }
  const auto cdbOfShared = sharedCDB::arbitrate(cdbOfMul, cdbOfDiv, cdbOfLQ);
  DispatchBus dispatchBus = DispatchArbiter::arbitrate(
      RSModule, ALUModule, AGUModule, BRUModule, MULModule, DIVModule,
      PRFModule, squashDetect, cdbOfMul, cdbOfDiv, cdbOfDiv.valid);
  if (recovery.active) dispatchBus = {};
  aguInput.squashDetect = squashDetect;
  aluInput.squashDetect = squashDetect;
  aluInput.cdbOutput = cdbOfALU;
  aluInput.dispatch = dispatchBus.alu;
  mulInput.squashDetect = squashDetect;
  mulInput.cdbOutput = cdbOfMul;
  mulInput.dispatch = dispatchBus.mul;
  divInput.squashDetect = squashDetect;
  divInput.cdbOutput = cdbOfDiv;
  divInput.outReady = cdbOfDiv.valid;
  divInput.dispatch = dispatchBus.div;
  aguInput.dispatch = dispatchBus.agu;
  bruInput.dispatch = dispatchBus.bru;
  rsInput.dispatchBus = dispatchBus;
  decodeInput.squashDetect = squashDetect;
  lqInput.squashDetect = squashDetect;
  sqInput.squashDetect = squashDetect;
  auto memDispatch = MemArbiter::arbitrate(LQModule, SQModule, ROBModule,
                                            DCacheModule, squashDetect);
  if (exitCommitted && (memDispatch.request.op != Operation::Store || SQModule.isEmpty() ||
      !SQModule.isCommitted(SQModule.getHead() & SQ_MASK))) memDispatch.valid = false;
  dcacheInput.decision = memDispatch;
  dcacheInput.squashDetect = squashDetect;
  lqInput.decision = memDispatch;
  sqInput.decision = memDispatch;
  // store value-ready broadcast (data event): scan ready storeValueRS entries
  // against the SQ snapshot (store address must already be known)
  for (int i = 0; i < STORERS_CAP; ++i) {
    lqInput.storeNotifies[i] = StoreNotify{};
    if (!recovery.active && !RSModule.storeValueRS[i].free &&
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
  if (!recovery.active && !AGUModule.isEmpty() && isStoreMem(AGUModule.headMemIndex())) {
    auto aguRobTag = AGUModule.headRobTag();
    if (!squashDetect.needSquash ||
        (squashDetect.needSquash &&
         ROB::isOlder(aguRobTag, squashDetect.SquashTag))) {
      lqInput.storeAddrNotify = SQModule.planAddressForward(
          memSlot(AGUModule.headMemIndex()), AGUModule.headValue(), aguRobTag);
    }
  }
  rsInput.squashDetect = squashDetect;
  robInput.squashDetect = squashDetect;
  robInput.storeCompletion = SQModule.completionBitmap();
  robInput.cdbOfALU = cdbOfALU;
  robInput.cdbOfShared = cdbOfShared;
  prfInput.squashDetect = squashDetect;
  prfInput.cdbOfALU = cdbOfALU;
  prfInput.cdbOfShared = cdbOfShared;
  ratInput.squashDetect = squashDetect;
  flarbInput.squashDetect = squashDetect;
  flarbInput.cdbOut = cdbOfALU;
  bruInput.squashDetect = squashDetect;
  bpuInput.squashDetect = squashDetect;
  bpuInput.cdbOut = cdbOfALU;
  bpuInput.fetchInfo = scanJump(FQModule.getLastPush());
  lqInput.cdbOutput = cdbOfLQ;
  // SRAM combination logic
  icacheDataInput.enable = icacheInput.lineReturn.valid || fetchDecision.valid;
  icacheDataInput.writeEnable = icacheInput.lineReturn.valid;
  icacheDataInput.addr =
      ((icacheInput.lineReturn.valid ? icacheInput.lineReturn.lineAddr
                                     : fetchDecision.pc) >>
       ICACHE_OFFSET_BITS) &
      ICACHE_INDEX_MASK;
  for (int i = 0; i < ICACHE_WORDS_PER_LINE; ++i) {
    icacheDataInput.writeData[i] = icacheInput.lineReturn.data[i];
    icacheDataInput.writeMask[i] = true;
  }
  // D$ owns miss/response/SRAM arbitration. These are the exact same accepted
  // line events consumed by DMEM; Load completion still enters the old LQ/CDB
  // pipeline and never bypasses into a dependent dispatch in this clock.
  DCacheModule.plan(dcacheInput);
  lqInput.loadResp = dcacheInput.loadResp;
  axiFetchInput.squash = squashDetect.needSquash;
  axiFetchInput.fetchValid = imemInput.fetchDecision.valid;
  axiFetchInput.fetchPC = imemInput.fetchDecision.pc;
  axiFetchInput.lineConsumed = fillFire && !exitCommitted;
  axiDataInput.request = dcacheInput.memoryRequest;
  axiDataInput.replyConsumed = dcacheInput.fillFire;
  axiDataInput.writeConsumed = dcacheInput.writeAck >= 0;
  axiDataInput.exitValid = dcacheInput.exitValid;
  axiDataInput.exitValue = dcacheInput.exitValue;
  axiBusInput = {AXIFetchModule.port(), AXIDataModule.port(), SlaveModule.port()};
  const auto ports = AXIBusModule.ports(axiBusInput);
  axiFetchInput.bus = ports.fetch;
  axiDataInput.bus = ports.data;
  slaveInput = ports.slave;
}

void CPU::clock() {
  if (!exitCommitted) {
    if (squashDetect.needSquash) {
      assert(!recovery.active && ROBModule.matchesTag(squashDetect.SquashTag));
      CPUstate.recovery.active = true;
      CPUstate.recovery.beat = 0;
      CPUstate.recovery.offset = 0;
      CPUstate.recovery.nextTag = ROBModule.getHead();
      CPUstate.recovery.squashTag = squashDetect.SquashTag;
      CPUstate.recovery.liveCount = (ROBModule.getNextTag() - ROBModule.getHead()) & ROB_TAG_MASK;
    } else if (recovery.active) {
      RobTag next = recovery.nextTag;
      for (unsigned lane = 0; lane < RECOVERY_WIDTH; ++lane) next = robNextTag(next);
      CPUstate.recovery.nextTag = next;
      CPUstate.recovery.offset = recovery.offset + RECOVERY_WIDTH;
      if (recovery.beat == RECOVERY_BEATS - 1) CPUstate.recovery.active = false;
      else CPUstate.recovery.beat = recovery.beat + 1;
    }
  }
  AXIFetchModule.tick(axiFetchInput, CPUstate);
  AXIDataModule.tick(axiDataInput, CPUstate);
  AXIBusModule.tick(axiBusInput, CPUstate);
  SlaveModule.tick(slaveInput, CPUstate);
  DCacheModule.tick(dcacheInput, CPUstate);
  SQModule.tick(sqInput, CPUstate);
  if (!exitCommitted) {
   ICacheModule.tick(icacheInput, CPUstate);
   LQModule.tick(lqInput, CPUstate);
   PRFModule.tick(prfInput, CPUstate);
   BPUModule.tick(bpuInput, CPUstate);
   RATModule.tick(ratInput, CPUstate);
   if (!recovery.active) {
  FetchUnitModule.tick(fetchUnitInput, CPUstate);
  FQModule.tick(fqInput, CPUstate);
  ROBModule.tick(robInput, CPUstate);
  ALUModule.tick(aluInput, CPUstate);
  MULModule.tick(mulInput, CPUstate);
  DIVModule.tick(divInput, CPUstate);
  AGUModule.tick(aguInput, CPUstate);
  BRUModule.tick(bruInput, CPUstate);
  RSModule.tick(rsInput, CPUstate);
  flushArbiter.tick(flarbInput, CPUstate);
  DecodeUnitModule.tick(decodeInput, CPUstate);
   }
  }
  if (exitRetiring) CPUstate.exitCommitted = true;
  CPUstate.initialized = true;
}

void CPU::run() {
  bool finish = false;
  uint64_t clock = 0;
  uint64_t initCycles = 0;
  while (!CPUstate.initialized) {
    comb();
    this->clock();
    ++initCycles;
  }
  // host-only: core IPC freezes on the HALT-marker commit. The marker itself
  // is not an executed architectural instruction, while its commit cycle is
  // part of the measured interval.
  uint64_t retired = 0;
  uint64_t ipcRetired = 0;
  uint64_t ipcCycles = 0;
  bool ipcFrozen = false;
  while (!finish) {
    comb();
    const uint32_t headBefore = static_cast<uint32_t>(ROBModule.getHead());
    const bool haltBefore = ROBModule.isHaltCommitted();
    this->clock();
    ++clock;
    if (CPUstate.ROBModule.isTrapCommitted())
      throw std::runtime_error("illegal instruction committed");
    const uint32_t headAfter =
        static_cast<uint32_t>(CPUstate.ROBModule.getHead());
    const bool haltAfter = CPUstate.ROBModule.isHaltCommitted();
    unsigned committed = 0;
    RobTag cursor = headBefore;
    for (unsigned lane = 0; lane < COMMIT_WIDTH; ++lane) {
      if (cursor != headAfter) { ++committed; cursor = robNextTag(cursor); }
    }
    assert(cursor == headAfter);
    const bool haltCommitted = !haltBefore && haltAfter;
    assert(!haltCommitted || committed != 0);
    if (!ipcFrozen) {
      retired += committed - static_cast<uint32_t>(haltCommitted);
      if (haltCommitted || exitRetiring) {
        ipcRetired = retired;
        ipcCycles = clock;
        ipcFrozen = true;
      }
    }
    // User contract: stop at HALT commit, without SQ/cache/DMEM drain.
    finish = haltAfter || CPUstate.SlaveModule.isDone();
  }
  if (debug::enabled(debug::TOPIC_CLOCK)) {
    debug::print("clock: %llu\n", clock);
    debug::print("timing: init=%llu core=%llu drain=%llu total=%llu\n",
                 initCycles, ipcCycles, clock - ipcCycles, clock);
    debug::print("ipc: %.6f retired=%llu cycles=%llu\n",
                 ipcCycles ? static_cast<double>(ipcRetired) /
                                 static_cast<double>(ipcCycles)
                           : 0.0,
                 ipcRetired, ipcCycles);
  }
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
    debug::print("branch-type: cond=%llu/%llu(%.2f%%) jal=%llu/%llu(%.2f%%) "
                 "jalr=%llu/%llu(%.2f%%)\n",
                 bp.getCondCorrect(), bp.getCondTotal(),
                 pct(bp.getCondCorrect(), bp.getCondTotal()),
                 bp.getJalCorrect(), bp.getJalTotal(),
                 pct(bp.getJalCorrect(), bp.getJalTotal()), bp.getJalrCorrect(),
                 bp.getJalrTotal(),
                 pct(bp.getJalrCorrect(), bp.getJalrTotal()));
  }
  // host-only: same end-of-run report as the branch block above.
  if (debug::enabled(debug::TOPIC_ICACHE)) {
    uint32_t h = CPUstate.ICacheModule.getHitCount();
    uint32_t m = CPUstate.ICacheModule.getMissCount();
    uint32_t t = h + m;
    debug::print("icache: hits=%u misses=%u total=%u hit-rate=%.2f%%\n", h, m,
                 t, t ? 100.0 * h / t : 0.0);
    uint64_t dh = CPUstate.DCacheModule.getHitCount();
    uint64_t dm = CPUstate.DCacheModule.getMissCount();
    uint64_t dt = dh + dm;
    debug::print("dcache: hits=%llu misses=%llu total=%llu hit-rate=%.2f%%\n",
                 dh, dm, dt,
                 dt ? 100.0 * static_cast<double>(dh) / static_cast<double>(dt)
                    : 0.0);
  }
  const uint32_t halt_x10 = CPUstate.SlaveModule.isDone() ? CPUstate.SlaveModule.exitResult() : static_cast<uint32_t>(
      CPUstate.PRFModule.getValue(
          CPUstate.RATModule.readRAT(CPUstate.ROBModule.getHaltRd())));
  const bool print_full_x10 = std::getenv("RESULT_FULL") != nullptr;
  std::cout << std::dec << ((print_full_x10 || CPUstate.SlaveModule.isDone()) ? halt_x10 : (halt_x10 & 0xFF))
            << std::endl;
}
