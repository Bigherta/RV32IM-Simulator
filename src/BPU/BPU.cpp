#include "../include/BPU.hpp"
#include "../include/CPU.hpp"
#include "../include/util.hpp"
#include <cstdint>
#include <cstring>

FetchDecision FetchDecision::build(const BPU &bp, uint32_t pc,
                                   const SquashInfo &squash, bool haltFetched,
                                   bool fqFull, bool imemReqFull) {
  FetchDecision fdec{};
  if (!squash.needSquash && !haltFetched && !fqFull && !imemReqFull) {
    auto prediction = bp.predict(pc);
    fdec.valid = true;
    fdec.pc = pc;
    fdec.predictedPC = prediction.taken ? prediction.predictPC : pc + 4;
    if (prediction.btbHit) {
      fdec.shift = true;
      if (prediction.unconditional) {
        fdec.shiftValue = true;
      } else {
        fdec.shiftValue = prediction.taken;
      }
    } else if (prediction.condSeen) {
      // Conditional not BTB-resident (e.g. never-taken): its outcome still
      // belongs in the GHR, otherwise history membership would depend on
      // BTB residency churn.
      fdec.shift = true;
      fdec.shiftValue = prediction.taken;
    }
    fdec.ckptId = bp.getNextCkptId();
  }
  return fdec;
}

PredictInfo BPU::predict(uint32_t pc) const {
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  const uint32_t localIndex = p2 & (BHT_CAP - 1);
  const uint32_t globalIndex = (p2 ^ dir.GHR) & (BHT_CAP - 1);
  const uint32_t selectorIndex = (p2 ^ dir.GHR) & (SELECTOR_CAP - 1);
  const bool useGlobal = dir.selector[selectorIndex] >= 2;
  const bool directionTaken = useGlobal ? dir.globalPHT[globalIndex] >= 2
                                        : dir.localPHT[localIndex] >= 2;
  const auto BTB_index = p2 & (BTB_CAP - 1);
  bool btbHit = tgt.BTB[BTB_index].valid &&
                tgt.BTB[BTB_index].actualPC == static_cast<uint32_t>(pc);
  bool taken = btbHit && directionTaken;
  if (btbHit && tgt.BTB[BTB_index].unconditional)
    taken = true;

  // Target Cache: per-pc local-history hashed target for true indirect
  // jumps (JALR). BHR is the committed 8b outcome history of branches
  // landing in the same BHT slot; pc^BHR separates the dynamic contexts
  // under which one static indirect site dispatches to different targets.
  // RET with empty RAS: don't use BTB target 0, treat as not taken (wild fetch
  // fix)
  bool isRet = tgt.BTB[BTB_index].isRet;
  bool rasEmpty = tgt.RAS_top == 0;
  if (isRet && rasEmpty) {
    btbHit = false;
    taken = false;
  }
  // uint32 bit-vector add: signed uint32_t add past the range is host UB.
  uint32_t predictPC = static_cast<uint32_t>(static_cast<uint32_t>(pc) + 4u);
  if (taken && btbHit) {
    if (isRet && tgt.RAS_top > 0)
      predictPC = static_cast<uint32_t>(
          tgt.RAS[(tgt.RAS_top - 1) & (RAS_CAP - 1)].retPC);
    else
      predictPC = tgt.BTB[BTB_index].target;
  }

  PredictInfo out{taken, predictPC};
  out.btbHit = btbHit;
  out.unconditional = btbHit && tgt.BTB[BTB_index].unconditional;
  out.condSeen = tgt.condSeen[p2 & (CONDSEEN_CAP - 1)];
  return out;
}

void BPU::update(uint32_t pc, bool taken, uint32_t target, uint8_t ghr) {
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  const uint32_t localIndex = p2 & (BHT_CAP - 1);
  const uint32_t globalIndex = (p2 ^ ghr) & (BHT_CAP - 1);
  const uint32_t selectorIndex = (p2 ^ ghr) & (SELECTOR_CAP - 1);
  const bool localPred = dir.localPHT[localIndex] >= 2;
  const bool globalPred = dir.globalPHT[globalIndex] >= 2;

  auto &local = dir.localPHT[localIndex];
  auto &global = dir.globalPHT[globalIndex];
  if (taken) {
    if (local < 3)
      ++local;
    if (global < 3)
      ++global;
  } else {
    if (local > 0)
      --local;
    if (global > 0)
      --global;
  }

  auto &choice = dir.selector[selectorIndex];
  if (globalPred == taken && localPred != taken) {
    if (choice < 3)
      ++choice;
  } else if (localPred == taken && globalPred != taken) {
    if (choice > 0)
      --choice;
  }

  tgt.condSeen[p2 & (CONDSEEN_CAP - 1)] = true;

  // BTB train on taken conditional
  auto BTB_index = p2 & (BTB_CAP - 1);
  if (taken) {
    tgt.BTB[BTB_index].actualPC = static_cast<uint32_t>(pc);
    tgt.BTB[BTB_index].target = target;
    tgt.BTB[BTB_index].valid = true;
    tgt.BTB[BTB_index].unconditional = false;
    tgt.BTB[BTB_index].isRet = false;
  }

  // Committed target history: every resolved branch folds its outcome into
  // the per-slot 8b BHR consumed by the Target Cache hash.
}

void BPU::updateJump(uint32_t pc, uint32_t target, bool isRet) {
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  auto BTB_index = p2 & (BTB_CAP - 1);
  tgt.BTB[BTB_index].actualPC = static_cast<uint32_t>(pc);
  tgt.BTB[BTB_index].target = target;
  tgt.BTB[BTB_index].valid = true;
  tgt.BTB[BTB_index].unconditional = true;
  tgt.BTB[BTB_index].isRet = isRet;

  // true indirect jump: train Target Cache at this context's hash.
  // Direct JALs never touch TC — their BTB target is exact and must not
  // be overridable through a colliding history hash.
}

void BPU::dumpBpMiss() const {
  uint64_t total = 0;
  for (int i = 0; i < BTB_CAP; ++i)
    total += missCnt[i];
  debug::print("bpmiss: %llu total, top PCs (pc[11:2] aliased):\n", total);
  bool used[BTB_CAP] = {};
  for (int n = 0; n < 16; ++n) {
    int best = -1;
    for (int i = 0; i < BTB_CAP; ++i)
      if (!used[i] && (best < 0 || missCnt[i] > missCnt[best]))
        best = i;
    if (best < 0 || missCnt[best] == 0)
      break;
    used[best] = true;
    debug::print("  pc 0x%04x: %llu\n", missPC[best], missCnt[best]);
  }
}

void BPU::shiftGHR(bool taken) {
  dir.GHR = static_cast<uint8_t>(
      ((static_cast<uint32_t>(dir.GHR) << 1) | (taken ? 1u : 0u)) &
      HISTORY_MASK);
}

BPUSnapshot BPU::snapshotCheckPoint() const {
  BPUSnapshot s;
  s.GHR_snapshot = dir.GHR;
  s.alignTail = tgt.alignTail;
  s.RAS_top = tgt.RAS_top;
  return s;
}

void BPU::recoverCheckPoint(const BPUSnapshot &ckpt) {
  dir.GHR = ckpt.GHR_snapshot;
  tgt.alignTail = ckpt.alignTail;
  tgt.RAS_top = ckpt.RAS_top;
}

void BPU::tick(const BPUInput &input, systemState &CPUstate) {
  Cand bru, cdb;
  if (!input.BRUModule.isEmpty() &&
      input.ROBModule.matchesTag(input.BRUModule.headRobTag())) {
    uint8_t brRobTag = input.BRUModule.headRobTag();
    const uint32_t pcResult = input.BRUModule.headPCResult();
    const uint32_t pcFrom = input.BRUModule.headPCFrom();
    {
      ++CPUstate.BPUModule.branchTotal;
      // BRU resolves conditional branches only -> class = cond.
      ++CPUstate.BPUModule.condTotal;
      const uint32_t predictedPC = static_cast<uint32_t>(
          input.ROBModule.getPredictedPC(robSlot(brRobTag)));
      bool correct = pcResult == predictedPC;
      if (correct) {
        ++CPUstate.BPUModule.branchCorrect;
        ++CPUstate.BPUModule.condCorrect;
      } else
        CPUstate.BPUModule.noteMiss(pcFrom);
      if (!input.squashDetect.needSquash ||
          (input.squashDetect.needSquash &&
           ROB::isOlder(brRobTag, input.squashDetect.SquashTag))) {
        bru.valid = true;
        // PC values are uint32 bit vectors.
        bru.pc = static_cast<uint32_t>(pcFrom);
        bru.taken = pcResult != pcFrom + 4u;
        bru.target = static_cast<uint32_t>(pcResult);
        const uint8_t cid = input.ROBModule.getCkptId(robSlot(brRobTag));
        bru.ghr = bpCkpt[cid].GHR_snapshot;
      }
    }
  }
  const auto &cdbOut = input.cdbOut;
  if (cdbOut.valid && cdbOut.isControl &&
      input.ROBModule.matchesTag(cdbOut.robTag)) {
    auto robIdx = robSlot(cdbOut.robTag);
    const auto pc = static_cast<uint32_t>(cdbOut.value);
    if (!input.squashDetect.needSquash ||
        (input.squashDetect.needSquash &&
         ROB::isOlder(cdbOut.robTag, input.squashDetect.SquashTag))) {
      ++CPUstate.BPUModule.branchTotal;
      // CDB control transfers are JAL/JALR (both decode to Operation::JALR;
      // the ROB distinguishes them): direct JAL has isIndirect == false,
      // register-driven JALR has isIndirect == true.
      const bool isJalr = input.ROBModule.isIndirect(robIdx);
      if (isJalr)
        ++CPUstate.BPUModule.jalrTotal;
      else
        ++CPUstate.BPUModule.jalTotal;
      bool correct = pc == input.ROBModule.getPredictedPC(robIdx);
      if (correct) {
        ++CPUstate.BPUModule.branchCorrect;
        if (isJalr)
          ++CPUstate.BPUModule.jalrCorrect;
        else
          ++CPUstate.BPUModule.jalCorrect;
      } else
        // record the jump SITE, not its target: targets are arbitrary
        // addresses that would poison the per-PC miss profile.
        CPUstate.BPUModule.noteMiss(
            static_cast<uint32_t>(input.ROBModule.getPC(robIdx)));
      cdb.valid = true;
      cdb.pc = input.ROBModule.getPC(robIdx);
      cdb.taken = true;
      cdb.target = static_cast<uint32_t>(pc);
      cdb.ghr = bpCkpt[input.ROBModule.getCkptId(robIdx)].GHR_snapshot;
      cdb.cond = false;
      cdb.isRet = input.ROBModule.isRet(robIdx);
    }
  }

  auto apply = [&](const Cand &c) {
    if (c.cond)
      CPUstate.BPUModule.update(c.pc, c.taken, c.target, c.ghr);
    else
      CPUstate.BPUModule.updateJump(c.pc, c.target, c.isRet);
  };
  if (bru.valid)
    apply(bru);
  if (cdb.valid)
    apply(cdb);

  const auto &fd = input.fetchDecision;
  if (fd.valid) {
    CPUstate.BPUModule.bpCkpt[fd.ckptId] = snapshotCheckPoint();
    if (fd.shift)
      CPUstate.BPUModule.shiftGHR(fd.shiftValue);
    CPUstate.BPUModule.nextCkptId = (fd.ckptId + 1) & (CKPT_CAP - 1);
  }
  // Pre-decode scanner: RAS maintenance keyed on decoded instruction type
  // (routed from the FQ push of the PREVIOUS cycle), never on BTB hits.
  // Journal + checkpoint-rewind machinery is unchanged -- only the event
  // source moved. Must stay BEFORE the squash-recover block: events landing
  // this tick belong to fetches younger than the squash point and must be
  // undone by it.
  const auto &fi = input.fetchInfo;
  if (fi.valid) {
    const uint32_t ra = fi.pc + 4;
    if (fi.isCall) {
      uint32_t topIdx = tgt.RAS_top & (RAS_CAP - 1);
      if (tgt.RAS_top > 0 &&
          tgt.RAS[(tgt.RAS_top - 1) & (RAS_CAP - 1)].retPC == ra) {
        AlignEntry e;
        e.addr = tgt.RAS[(tgt.RAS_top - 1) & (RAS_CAP - 1)].retPC;
        e.index = (tgt.RAS_top - 1) & (RAS_CAP - 1);
        e.times = tgt.RAS[(tgt.RAS_top - 1) & (RAS_CAP - 1)].times;
        CPUstate.BPUModule.tgt.alignQueue[tgt.alignTail & (ALIGNQ_CAP - 1)] = e;
        CPUstate.BPUModule.tgt.alignTail++;
        CPUstate.BPUModule.tgt.RAS[(tgt.RAS_top - 1) & (RAS_CAP - 1)].times++;
      } else {
        CPUstate.BPUModule.tgt.RAS[topIdx].retPC = ra;
        CPUstate.BPUModule.tgt.RAS[topIdx].times = 1;
        CPUstate.BPUModule.tgt.RAS_top++;
      }
    } else if (fi.isRet && tgt.RAS_top > 0) {
      uint32_t topIdx = (tgt.RAS_top - 1) & (RAS_CAP - 1);
      AlignEntry e;
      e.addr = tgt.RAS[topIdx].retPC;
      e.index = static_cast<uint8_t>(topIdx);
      e.times = tgt.RAS[topIdx].times;
      CPUstate.BPUModule.tgt.alignQueue[tgt.alignTail & (ALIGNQ_CAP - 1)] = e;
      CPUstate.BPUModule.tgt.alignTail++;
      if (tgt.RAS[topIdx].times > 1)
        CPUstate.BPUModule.tgt.RAS[topIdx].times--;
      else
        CPUstate.BPUModule.tgt.RAS_top--;
    }
    // Early BTB type/target training: jal carries its static target in the
    // encoding, so direct calls become perfectly predicted from their second
    // encounter without waiting for a resolve. Writes MUST go to
    // CPUstate.BPUModule (the committed state): tick() executes on the
    // comb-snapshot copy, and bare tgt writes here never persisted.
    if (fi.isCall || (!fi.isCall && !fi.isRet)) {
      auto BTB_index = (fi.pc >> 2) & (BTB_CAP - 1);
      CPUstate.BPUModule.tgt.BTB[BTB_index].actualPC = fi.pc;
      CPUstate.BPUModule.tgt.BTB[BTB_index].valid = true;
      CPUstate.BPUModule.tgt.BTB[BTB_index].unconditional = true;
      CPUstate.BPUModule.tgt.BTB[BTB_index].isRet = false;
      if (fi.jalTargetValid)
        CPUstate.BPUModule.tgt.BTB[BTB_index].target = fi.jalTarget;
    }
    if (fi.isRet) {
      auto BTB_index = (fi.pc >> 2) & (BTB_CAP - 1);
      CPUstate.BPUModule.tgt.BTB[BTB_index].actualPC = fi.pc;
      CPUstate.BPUModule.tgt.BTB[BTB_index].valid = true;
      CPUstate.BPUModule.tgt.BTB[BTB_index].unconditional = true;
      CPUstate.BPUModule.tgt.BTB[BTB_index].isRet = true;
    }
  }
  if (input.squashDetect.needSquash) {
    const auto &ckpt = bpCkpt[input.squashDetect.CkptId];
    uint8_t curTail = tgt.alignTail;
    uint8_t base = ckpt.alignTail;
    uint8_t dist = curTail - base;
    for (int k = 0; k < ALIGNQ_CAP; ++k) {
      if ((uint8_t)k >= dist)
        continue;
      uint8_t pos = curTail - 1 - (uint8_t)k;
      AlignEntry e = tgt.alignQueue[pos & (ALIGNQ_CAP - 1)];
      CPUstate.BPUModule.tgt.RAS[e.index & (RAS_CAP - 1)].retPC = e.addr;
      CPUstate.BPUModule.tgt.RAS[e.index & (RAS_CAP - 1)].times = e.times;
    }
    CPUstate.BPUModule.recoverCheckPoint(ckpt);
    CPUstate.BPUModule.nextCkptId =
        (input.squashDetect.CkptId + 1) & (CKPT_CAP - 1);
  }
}
