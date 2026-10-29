#include "../include/BPU.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

FetchDecision FetchDecision::build(const BPU &bp, uint32_t pc,
                                   const SquashInfo &squash, bool haltFetched,
                                   bool fqCanAccept, bool memCanAccept) {
  FetchDecision fdec{};
  if (!squash.needSquash && !haltFetched && fqCanAccept && memCanAccept) {
    auto prediction = bp.predict(pc);
    fdec.valid = true;
    fdec.count = 1;
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
  const uint32_t localIndex = ((p2 << LOCAL_HISTORY_BITS) | dir.localHistory[p2 & (BHT_CAP - 1)]) & (BHT_CAP - 1);
  const uint32_t globalIndex = (p2 ^ dir.GHR) & (BHT_CAP - 1);
  const uint32_t selectorIndex = p2 & (SELECTOR_CAP - 1);
  const bool useGlobal = dir.selector[selectorIndex] >= 2;
  const bool directionTaken = useGlobal ? dir.globalPHT[globalIndex] >= 2
                                        : dir.localPHT[localIndex] >= 4;
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
  bool rasEmpty = tgt.specTopOfRAS == 0;
  if (isRet && rasEmpty) {
    btbHit = false;
    taken = false;
  }
  // uint32 bit-vector add: signed uint32_t add past the range is host UB.
  uint32_t predictPC = static_cast<uint32_t>(static_cast<uint32_t>(pc) + 4u);
  if (taken && btbHit) {
    if (isRet && tgt.specTopOfRAS > 0)
      predictPC = tgt.specRAS[tgt.specTopOfRAS - 1];
    else
      predictPC = tgt.BTB[BTB_index].target;
  }

  PredictInfo out{taken, predictPC};
  out.btbHit = btbHit;
  out.unconditional = btbHit && tgt.BTB[BTB_index].unconditional;
  out.condSeen = tgt.condSeen[p2 & (CONDSEEN_CAP - 1)];
  return out;
}

DirectionContext BPU::contextAt(uint32_t pc) const {
  const uint32_t p2 = pc >> 2;
  const auto history = dir.localHistory[p2 & (BHT_CAP - 1)];
  return {history,
          dir.localPHT[((p2 << LOCAL_HISTORY_BITS) | history) & (BHT_CAP - 1)] >= 4,
          dir.globalPHT[(p2 ^ dir.GHR) & (BHT_CAP - 1)] >= 2};
}

void BPU::update(uint32_t pc, bool taken, uint32_t target, uint8_t ghr,
                 const DirectionContext &context, uint8_t resolvedHistory,
                 systemState &CPUstate) const {
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  const uint32_t historyIndex = p2 & (BHT_CAP - 1);
  const uint32_t localIndex = ((p2 << LOCAL_HISTORY_BITS) | context.localHistory) & (BHT_CAP - 1);
  const uint32_t globalIndex = (p2 ^ ghr) & (BHT_CAP - 1);
  const uint32_t selectorIndex = p2 & (SELECTOR_CAP - 1);
  const bool localPred = context.localPrediction;
  const bool globalPred = context.globalPrediction;

  auto local = dir.localPHT[localIndex];
  auto global = dir.globalPHT[globalIndex];
  if (taken) {
    if (local < 7)
      ++local;
    if (global < 3)
      ++global;
  } else {
    if (local > 0)
      --local;
    if (global > 0)
      --global;
  }

  auto choice = dir.selector[selectorIndex];
  if (globalPred == taken && localPred != taken) {
    if (choice < 3)
      ++choice;
  } else if (localPred == taken && globalPred != taken) {
    if (choice > 0)
      --choice;
  }

  CPUstate.BPUModule.dir.localPHT[localIndex] = local;
  CPUstate.BPUModule.dir.globalPHT[globalIndex] = global;
  CPUstate.BPUModule.dir.selector[selectorIndex] = choice;
  CPUstate.BPUModule.dir.localHistory[historyIndex] =
      ((resolvedHistory << 1) | unsigned(taken)) & LOCAL_HISTORY_MASK;
  CPUstate.BPUModule.tgt.condSeen[p2 & (CONDSEEN_CAP - 1)] = true;

  // BTB train on taken conditional
  auto BTB_index = p2 & (BTB_CAP - 1);
  if (taken) {
    CPUstate.BPUModule.tgt.BTB[BTB_index].actualPC = static_cast<uint32_t>(pc);
    CPUstate.BPUModule.tgt.BTB[BTB_index].target = target;
    CPUstate.BPUModule.tgt.BTB[BTB_index].valid = true;
    CPUstate.BPUModule.tgt.BTB[BTB_index].unconditional = false;
    CPUstate.BPUModule.tgt.BTB[BTB_index].isRet = false;
  }

}

void BPU::updateJump(uint32_t pc, uint32_t target, bool isRet, systemState &CPUstate) const {
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  auto BTB_index = p2 & (BTB_CAP - 1);
  CPUstate.BPUModule.tgt.BTB[BTB_index].actualPC = static_cast<uint32_t>(pc);
  CPUstate.BPUModule.tgt.BTB[BTB_index].target = target;
  CPUstate.BPUModule.tgt.BTB[BTB_index].valid = true;
  CPUstate.BPUModule.tgt.BTB[BTB_index].unconditional = true;
  CPUstate.BPUModule.tgt.BTB[BTB_index].isRet = isRet;

}

void BPU::shiftGHR(bool taken, systemState &CPUstate) const {
  CPUstate.BPUModule.dir.GHR = static_cast<uint8_t>(
      ((static_cast<uint32_t>(dir.GHR) << 1) | (taken ? 1u : 0u)) &
      HISTORY_MASK);
}

uint8_t BPU::snapshotCheckPoint() const { return dir.GHR; }

void BPU::recoverCheckPoint(const uint8_t ghr, systemState &CPUstate) const {
  CPUstate.BPUModule.dir.GHR = ghr;
}

void BPU::tick(const BPUInput &input, systemState &CPUstate) {
  uint64_t total = branchTotal, totalCorrect = branchCorrect;
  Cand bru, cdb;
  if (!input.recovery.active && !input.BRUModule.isEmpty() &&
      input.ROBModule.matchesTag(input.BRUModule.headRobTag())) {
    uint8_t brRobTag = input.BRUModule.headRobTag();
    const uint32_t pcResult = input.BRUModule.headPCResult();
    const uint32_t pcFrom = input.BRUModule.headPCFrom();
    if (!input.squashDetect.needSquash ||
        (input.squashDetect.needSquash &&
         ROB::isOlder(brRobTag, input.squashDetect.SquashTag))) {
      ++total;
      // BRU resolves conditional branches only -> class = cond.
      CPUstate.BPUModule.condTotal = condTotal + 1;
      const uint32_t predictedPC = static_cast<uint32_t>(
          input.ROBModule.getPredictedPC(robSlot(brRobTag)));
      bool correct = pcResult == predictedPC;
      if (correct) {
        ++totalCorrect;
        CPUstate.BPUModule.condCorrect = condCorrect + 1;
      }
      bru.valid = true;
      // PC values are uint32 bit vectors.
      bru.pc = static_cast<uint32_t>(pcFrom);
      bru.taken = input.BRUModule.headActualTaken(); // use the actual taken to train
      bru.target = static_cast<uint32_t>(pcResult);
      const uint8_t cid = input.ROBModule.getCkptId(robSlot(brRobTag));
      bru.ghr = GHRCheckpoint[cid];
      bru.context = predictionContext[cid];
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
      ++total;
      // CDB control transfers are JAL/JALR (both decode to Operation::JALR;
      // the ROB distinguishes them): direct JAL has isIndirect == false,
      // register-driven JALR has isIndirect == true.
      const bool isJalr = input.ROBModule.isIndirect(robIdx);
      if (isJalr)
        CPUstate.BPUModule.jalrTotal = jalrTotal + 1;
      else
        CPUstate.BPUModule.jalTotal = jalTotal + 1;
      bool correct = pc == input.ROBModule.getPredictedPC(robIdx);
      if (correct) {
        ++totalCorrect;
        if (isJalr)
          CPUstate.BPUModule.jalrCorrect = jalrCorrect + 1;
        else
          CPUstate.BPUModule.jalCorrect = jalCorrect + 1;
      }
      cdb.valid = true;
      cdb.pc = input.ROBModule.getPC(robIdx);
      cdb.taken = true;
      cdb.target = static_cast<uint32_t>(pc);
      cdb.ghr = GHRCheckpoint[input.ROBModule.getCkptId(robIdx)];
      cdb.cond = false;
      cdb.isRet = input.ROBModule.isRet(robIdx);
    }
  }

  auto apply = [&](const Cand &c) {
    if (c.cond)
      update(c.pc, c.taken, c.target, c.ghr, c.context, input.resolvedLocalHistory, CPUstate);
    else
      updateJump(c.pc, c.target, c.isRet, CPUstate);
  };
  if (bru.valid)
    apply(bru);
  if (cdb.valid)
    apply(cdb);

  const auto &fd = input.fetchDecision;
  if (fd.valid) {
    for (unsigned lane = 0; lane < FETCH_WIDTH; ++lane) {
      if (lane < fd.count) {
        const unsigned cid = (fd.ckptId + lane) & (CKPT_CAP - 1);
        CPUstate.BPUModule.GHRCheckpoint[cid] = snapshotCheckPoint();
        CPUstate.BPUModule.predictionContext[cid] = input.fetchContexts[lane];
      }
    }
    if (fd.shift)
      shiftGHR(fd.shiftValue, CPUstate);
    CPUstate.BPUModule.nextCkptId = (fd.ckptId + fd.count) & (CKPT_CAP - 1);
  }
  // Pre-decode scanner: RAS maintenance keyed on decoded instruction type
  // (routed from the FQ push of the PREVIOUS cycle), never on BTB hits.
  // A pop-push overwrites the old top even though depth can be unchanged.
  // Squash reconstructs CONTENTS from the old architectural stack and live
  // ROB prefix; a depth-only checkpoint cannot undo that overwrite.
  unsigned specTop = tgt.specTopOfRAS;
  bool trainedFetch = false;
  for (unsigned lane = 0; lane < FETCH_WIDTH; ++lane) {
   const auto &fi = input.fetchInfo[lane];
   if (!input.recovery.active && fi.valid) {
    const uint32_t returnAddress = fi.pc + 4;
    if (!input.squashDetect.needSquash && (fi.isCall || fi.isRet)) {
      if (fi.isRet && specTop > 0) --specTop;
      if (fi.isCall && specTop < RAS_CAP) {
        CPUstate.BPUModule.tgt.specRAS[specTop] = returnAddress;
        ++specTop;
      }
      CPUstate.BPUModule.tgt.specTopOfRAS = specTop;
    }
    // Early BTB type/target training: jal carries its static target in the
    // encoding, so direct calls become perfectly predicted from their second
    // encounter without waiting for a resolve. Write only target fields;
    // conditions and indices come from the snapshot and input.
    if (!trainedFetch) {
    const auto BTB_index = (fi.pc >> 2) & (BTB_CAP - 1);
    CPUstate.BPUModule.tgt.BTB[BTB_index].actualPC = fi.pc;
    CPUstate.BPUModule.tgt.BTB[BTB_index].valid = true;
    CPUstate.BPUModule.tgt.BTB[BTB_index].unconditional = true;
    CPUstate.BPUModule.tgt.BTB[BTB_index].isRet = fi.isRet;
    if (fi.jalTargetValid && !fi.isRet)
      CPUstate.BPUModule.tgt.BTB[BTB_index].target = fi.jalTarget;
    trainedFetch = true;
    }
   }
  }

  // The committed wide word is single port: the redirect edge reads it,
  // normal retirement writes one masked lane. No commit overlaps recovery.
  SRAM<1, (RAS_CAP << 5), 32>::Input archPort{};
  archPort.enable = input.squashDetect.needSquash;
  auto archTop = tgt.archTopOfRAS;
  uint32_t archValid = tgt.archValid;
  for (unsigned lane = 0; lane < COMMIT_WIDTH; ++lane) {
   if (lane < input.commits.count) {
    const auto headIndex = robSlot(input.commits.tags[lane]);
    const bool push = input.ROBModule.isCall(headIndex);
    const bool pop = input.ROBModule.isRet(headIndex);
    if (push || pop) {
      if (pop && archTop > 0) --archTop;
      if (push && archTop < RAS_CAP) {
        archPort.enable = archPort.writeEnable = true;
        archPort.writeMask[archTop] = true;
        archPort.writeData[archTop] = static_cast<uint32_t>(input.ROBModule.getPC(headIndex)) + 4u;
        archValid |= 1u << archTop;
        ++archTop;
      }
    }
  }
  if (input.squashDetect.needSquash) {
    // recover GHR
    recoverCheckPoint(GHRCheckpoint[input.squashDetect.CkptId], CPUstate);
    assert(input.ROBModule.matchesTag(input.squashDetect.SquashTag));
    CPUstate.BPUModule.tgt.baselineTop = tgt.archTopOfRAS;
    // recover next checkpoint id
    CPUstate.BPUModule.nextCkptId =
        (input.squashDetect.CkptId + 1) & (CKPT_CAP - 1);
  }
  }
  if (input.commits.count != 0) {
    CPUstate.BPUModule.tgt.archTopOfRAS = archTop;
    CPUstate.BPUModule.tgt.archValid = archValid;
  }
  if (input.recovery.active) {
    const auto &step = input.recovery;
    unsigned top = step.first() ? tgt.baselineTop : tgt.specTopOfRAS;
    bool open = step.first() || tgt.replayOpen;
    uint32_t stack[RAS_CAP];
    for (int i = 0; i < RAS_CAP; ++i)
      stack[i] = step.first() ? ((tgt.archValid & (1u << i)) ? tgt.archStorage.readLane(i) : 0u) : tgt.specRAS[i];
    RobTag tag = step.nextTag;
    for (unsigned lane = 0; lane < RECOVERY_WIDTH; ++lane) {
      const unsigned offset = unsigned(step.offset) + lane;
      if (open && offset < ROB_CAP && offset < step.liveCount) {
        const auto &entry = input.replay[lane];
        if (entry.tag == tag) {
          if (entry.pop && top > 0) --top;
          if (entry.push && top < RAS_CAP)
            stack[top++] = entry.pc + 4u;
          if (tag == step.squashTag) open = false;
        } else open = false;
      }
      tag = robNextTag(tag);
    }
    for (int i = 0; i < RAS_CAP; ++i) CPUstate.BPUModule.tgt.specRAS[i] = stack[i];
    CPUstate.BPUModule.tgt.specTopOfRAS = top;
    CPUstate.BPUModule.tgt.replayOpen = open;
  }
  tgt.archStorage.tick(archPort, CPUstate.BPUModule.tgt.archStorage);
  CPUstate.BPUModule.branchTotal = total;
  CPUstate.BPUModule.branchCorrect = totalCorrect;
}
