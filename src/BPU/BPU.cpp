#include "../include/BPU.hpp"
#include "../include/CPU.hpp"
#include "../include/util.hpp"
#include <cstdint>
#include <cstring>

namespace {
// Fold the low `histLen` bits of GHR into `foldWidth` bits by XOR of
// successive foldWidth-bit chunks (Seznec folded history).
inline uint32_t refoldView(uint64_t ghr, int histLen, int foldWidth) {
  const uint32_t fmask =
      foldWidth >= 32 ? 0xffffffffu : ((1u << foldWidth) - 1u);
  if (histLen <= 0)
    return 0;
  if (histLen < 64)
    ghr &= (uint64_t{1} << histLen) - 1u;
  uint32_t r = 0;
  for (int s = 0; s < histLen; s += foldWidth)
    r ^= static_cast<uint32_t>(ghr >> s) & fmask;
  return r & fmask;
}
} // namespace

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
    fdec.meta = prediction.meta;
  }
  return fdec;
}

PredictInfo BPU::predict(int32_t pc) const {
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  const uint64_t ghr = dir.GHR;
  const uint32_t lhtIdx = p2 & (LHT_CAP - 1);
  const uint32_t t0index = (p2 ^ dir.LHT[lhtIdx]) & (T0_CAP - 1);
  const bool basePred = dir.t0[t0index] >= 2;
  bool hit[TAGE_NTABLES] = {};
  uint32_t idx[TAGE_NTABLES] = {};
  uint8_t tags[TAGE_NTABLES] = {};
  for (int i = 0; i < TAGE_NTABLES; ++i) {
    const int h = TAGE_HIST[i];
    idx[i] = (refoldView(ghr, h, TAGE_IDX_BIT) ^ (p2 & ((1u << TAGE_IDX_BIT) - 1))) &
             ((1u << TAGE_IDX_BIT) - 1);
    tags[i] = static_cast<uint8_t>(
        (refoldView(ghr, h, TAGE_TAG_BIT) ^ refoldView(ghr, h, TAGE_TAG_BIT - 1) ^
         (p2 & ((1u << TAGE_TAG_BIT) - 1))) &
        ((1u << TAGE_TAG_BIT) - 1));
    const auto &e = dir.tn[i][idx[i]];
    hit[i] = e.valid && e.tag == tags[i];
  }

  int prov = -1;
  int alt = -1;
  for (int i = TAGE_NTABLES - 1; i >= 0; --i) {
    if (hit[i]) {
      if (prov < 0)
        prov = i;
      else if (alt < 0)
        alt = i;
    }
  }

  bool altPred = basePred;
  if (alt >= 0)
    altPred = dir.tn[alt][idx[alt]].ctr >= 4;
  else if (prov >= 0)
    altPred = basePred;

  bool tagePred = basePred;
  uint8_t provCtr = 0;
  uint8_t provU = 0;
  bool provValid = false;
  if (prov >= 0) {
    provValid = true;
    provCtr = dir.tn[prov][idx[prov]].ctr;
    provU = dir.tn[prov][idx[prov]].u;
    const bool weak = (provCtr == 3 || provCtr == 4);
    const bool useAlt = dir.useAltOnNa[p2 & 127] >= 8;
    tagePred = (weak && useAlt) ? altPred : (provCtr >= 4);
  }

  // Statistical corrector removed (2026-08-24): the simplified 4-table
  // 6-bit-counter version degraded every benchmark (SUM +154k cycles);
  // a proper Seznec SC needs per-history-length counters + hysteresis.
  bool taken = tagePred;

  const auto BTB_index = p2 & (BTB_CAP - 1);
  bool btbHit =
      tgt.BTB[BTB_index].valid && tgt.BTB[BTB_index].actualPC == static_cast<uint32_t>(pc);
  if (btbHit && tgt.BTB[BTB_index].unconditional)
    taken = true;
  else if (!btbHit)
    taken = taken; // direction-only when no BTB; target falls through

  // Target Cache: per-pc local-history hashed target for true indirect
  // jumps (JALR). BHR is the committed 8b outcome history of branches
  // landing in the same BHT slot; pc^BHR separates the dynamic contexts
  // under which one static indirect site dispatches to different targets.
  const uint8_t bhr = tgt.BHT[p2 & (BHT_CAP - 1)];
  const uint32_t tcHash = (p2 ^ bhr) & (TARGETCACHE_CAP - 1);
  const bool tcUsable = btbHit && tgt.BTB[BTB_index].isIndirect &&
                        !tgt.BTB[BTB_index].isCall &&
                        !tgt.BTB[BTB_index].isRet &&
                        tgt.TargetValid[tcHash];
  // RET with empty RAS: don't use BTB target 0, treat as not taken (wild fetch fix)
  bool isRet = tgt.BTB[BTB_index].isRet;
  bool rasEmpty = tgt.RAS_top == 0;
  if (isRet && rasEmpty) {
    btbHit = false;
    taken = false;
  }
  int32_t predictPC = pc + 4;
  if (taken && btbHit) {
    if (isRet && tgt.RAS_top > 0)
      predictPC = static_cast<int32_t>(
          tgt.RAS[(tgt.RAS_top - 1) & (RAS_CAP - 1)].retPC);
    else if (tcUsable)
      predictPC = static_cast<int32_t>(tgt.TargetCache[tcHash]);
    else
      predictPC = tgt.BTB[BTB_index].target;
  }

  PredictInfo out{taken, predictPC};
  out.btbHit = btbHit;
  out.unconditional = btbHit && tgt.BTB[BTB_index].unconditional;
  out.meta.provValid = provValid;
  out.meta.provIdx = provValid ? static_cast<uint8_t>(prov) : 0;
  out.meta.provCtr = provCtr;
  out.meta.provU = provU;
  out.meta.altPred = altPred;
  out.meta.tagePred = tagePred;
  out.meta.baseCnt = dir.t0[t0index];
  out.condSeen = tgt.condSeen[p2 & (CONDSEEN_CAP - 1)];
  return out;
}

void BPU::update(int32_t pc, bool taken, int32_t target, uint64_t ghr,
                 const TAGESCMeta &meta) {
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  const uint64_t gh = ghr;

  // recompute indices/tags at resolve-time history (snapshot ghr)
  uint32_t idx[TAGE_NTABLES] = {};
  uint8_t tags[TAGE_NTABLES] = {};
  bool hit[TAGE_NTABLES] = {};
  for (int i = 0; i < TAGE_NTABLES; ++i) {
    const int h = TAGE_HIST[i];
    idx[i] = (refoldView(gh, h, TAGE_IDX_BIT) ^ (p2 & ((1u << TAGE_IDX_BIT) - 1))) &
             ((1u << TAGE_IDX_BIT) - 1);
    tags[i] = static_cast<uint8_t>(
        (refoldView(gh, h, TAGE_TAG_BIT) ^ refoldView(gh, h, TAGE_TAG_BIT - 1) ^
         (p2 & ((1u << TAGE_TAG_BIT) - 1))) &
        ((1u << TAGE_TAG_BIT) - 1));
    const auto &e = dir.tn[i][idx[i]];
    hit[i] = e.valid && e.tag == tags[i];
  }

  int prov = meta.provValid ? static_cast<int>(meta.provIdx) : -1;
  // record "this PC is a conditional" in the fetch-side type filter
  tgt.condSeen[p2 & (CONDSEEN_CAP - 1)] = true;
  const uint32_t lhtIdx = p2 & (LHT_CAP - 1);
  const uint32_t t0index = (p2 ^ dir.LHT[lhtIdx]) & (T0_CAP - 1);
  // train the local-history base at its hashed slot, then advance the
  // per-PC local history. Non-speculative: only correct-path resolutions
  // reach update(), so LHT content is committed-state by construction.
  if (taken) {
    if (dir.t0[t0index] < 3)
      ++dir.t0[t0index];
  } else {
    if (dir.t0[t0index] > 0)
      --dir.t0[t0index];
  }
  dir.LHT[lhtIdx] =
      static_cast<uint16_t>(((dir.LHT[lhtIdx] << 1) | (taken ? 1 : 0)) & 0xFFF);

  bool tageCorrect = (meta.tagePred == taken);
  if (prov >= 0 && hit[prov]) {
    auto &e = dir.tn[prov][idx[prov]];
    if (taken) {
      if (e.ctr < 7)
        ++e.ctr;
    } else {
      if (e.ctr > 0)
        --e.ctr;
    }
    // usefulness: provider correct & alt wrong -> +; provider wrong -> -
    if (tageCorrect && meta.altPred != taken) {
      if (e.u < 3)
        ++e.u;
    } else if (!tageCorrect) {
      if (e.u > 0)
        --e.u;
    }
  }

  // useAltOnNa: when provider was weak
  if (prov >= 0 && (meta.provCtr == 3 || meta.provCtr == 4)) {
    auto &ua = dir.useAltOnNa[p2 & 127];
    if (meta.altPred == taken && meta.tagePred != taken) {
      if (ua < 15)
        ++ua;
    } else if (meta.altPred != taken && meta.tagePred == taken) {
      if (ua > 0)
        --ua;
    }
  }

  // allocation on misprediction: try longer tables than provider.
  // Seznec no-alloc guard: when the ALT already predicted correctly while
  // the provider was confidently wrong, longer history would mostly capture
  // aliasing noise -- allocating then only churns useful rows.
  const bool provConfident =
      prov >= 0 && (meta.provCtr <= 1 || meta.provCtr >= 6);
  if (!tageCorrect && !(meta.altPred == taken && provConfident)) {
    const int start = prov + 1;
    bool allocated = false;
    // LFSR pick among free (u==0) slots
    uint8_t l = dir.lfsr;
    l = static_cast<uint8_t>((l & 1) ? ((l >> 1) ^ LFSR_TAPS) : (l >> 1));
    if (l == 0)
      l = LFSR_SEED;
    dir.lfsr = l;

    for (int k = 0; k < TAGE_NTABLES && !allocated; ++k) {
      // &(N-1), NOT modulo: TAGE_NTABLES is a power of two (guarded by the
      // static_assert next to its definition). A `%` here would put a real
      // divider on the mispredict-allocation path; the assert turns any
      // non-power-of-two table count into a compile error instead.
      int i = start + ((l >> (k << 1)) & (TAGE_NTABLES - 1));
      if (i < 0)
        i = 0;
      if (i >= TAGE_NTABLES)
        continue;
      if (!hit[i] || dir.tn[i][idx[i]].u == 0) {
        auto &e = dir.tn[i][idx[i]];
        e.valid = true;
        e.tag = tags[i];
        e.ctr = taken ? 4 : 3;
        e.u = 0;
        allocated = true;
      }
    }
    // if nothing free, decay u on a candidate
    if (!allocated && start < TAGE_NTABLES) {
      for (int i = start; i < TAGE_NTABLES; ++i) {
        auto &e = dir.tn[i][idx[i]];
        if (e.u > 0)
          --e.u;
      }
    }
  }

  // SC update removed (see predict()); tables deleted.

  // bankTick usefulness amnesty
  if (++dir.bankTickCtr >= BANKTICK_MAX) {
    dir.bankTickCtr = 0;
    for (int i = 0; i < TAGE_NTABLES; ++i)
      for (int j = 0; j < (1 << TAGE_IDX_BIT); ++j)
        dir.tn[i][j].u >>= 1;
  }

  // BTB train on taken conditional
  auto BTB_index = p2 & (BTB_CAP - 1);
  if (taken) {
    tgt.BTB[BTB_index].actualPC = static_cast<uint32_t>(pc);
    tgt.BTB[BTB_index].target = target;
    tgt.BTB[BTB_index].valid = true;
    tgt.BTB[BTB_index].unconditional = false;
    tgt.BTB[BTB_index].isCall = false;
    tgt.BTB[BTB_index].isRet = false;
    tgt.BTB[BTB_index].isIndirect = false;
  }

  // committed local-history shift: every resolved branch folds its outcome
  // into the per-slot 8b BHR consumed by the Target Cache hash
  uint8_t &bhrReg = tgt.BHT[p2 & (BHT_CAP - 1)];
  bhrReg = static_cast<uint8_t>(((bhrReg << 1) | (taken ? 1 : 0)) & 0xFF);
}

void BPU::updateJump(int32_t pc, int32_t target, bool isCall, bool isRet,
                     bool isIndirect) {
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  auto BTB_index = p2 & (BTB_CAP - 1);
  tgt.BTB[BTB_index].actualPC = static_cast<uint32_t>(pc);
  tgt.BTB[BTB_index].target = target;
  tgt.BTB[BTB_index].valid = true;
  tgt.BTB[BTB_index].unconditional = true;
  tgt.BTB[BTB_index].isCall = isCall;
  tgt.BTB[BTB_index].isRet = isRet;
  tgt.BTB[BTB_index].isIndirect = isIndirect;

  // true indirect jump: train Target Cache at this context's hash.
  // Direct JALs never touch TC — their BTB target is exact and must not
  // be overridable through a colliding history hash.
  const uint8_t bhr = tgt.BHT[p2 & (BHT_CAP - 1)];
  if (isIndirect && !isCall && !isRet) {
    const uint32_t tcHash = (p2 ^ bhr) & (TARGETCACHE_CAP - 1);
    tgt.TargetCache[tcHash] = static_cast<uint32_t>(target);
    tgt.TargetValid[tcHash] = true;
  }

  // committed local-history shift (unconditional jumps always taken)
  uint8_t &bhrReg = tgt.BHT[p2 & (BHT_CAP - 1)];
  bhrReg = static_cast<uint8_t>(((bhrReg << 1) | 1) & 0xFF);
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
  dir.GHR = ((dir.GHR << 1) | (taken ? 1u : 0u)) & HISTORY_MASK;
}

BPUSnapshot BPU::snapshotCheckPoint() const {
  BPUSnapshot s;
  s.GHR_snapshot = dir.GHR;
  s.alignHead = tgt.alignHead;
  s.alignTail = tgt.alignTail;
  s.RAS_top = tgt.RAS_top;
  return s;
}

void BPU::recoverCheckPoint(const BPUSnapshot &ckpt) {
  dir.GHR = ckpt.GHR_snapshot;
  tgt.alignHead = ckpt.alignHead;
  tgt.alignTail = ckpt.alignTail;
  tgt.RAS_top = ckpt.RAS_top;
}

void BPU::tick(const BPUInput &input, systemState &CPUstate) {
  Cand bru, cdb;
  if (!input.BRUModule.isEmpty()) {
    uint8_t brRobTag = input.BRUModule.headRobTag();
    int pcResult = input.BRUModule.headPCResult();
    int pcFrom = input.BRUModule.headPCFrom();
    {
      ++CPUstate.BPUModule.branchTotal;
      // BRU resolves conditional branches only -> class = cond.
      ++CPUstate.BPUModule.condTotal;
      bool correct = pcResult == input.ROBModule.getPredictedPC(
                                     ((brRobTag) & 0x3F));
      if (correct) {
        ++CPUstate.BPUModule.branchCorrect;
        ++CPUstate.BPUModule.condCorrect;
      } else
        CPUstate.BPUModule.noteMiss(static_cast<uint32_t>(pcFrom));
      if (!input.squashDetect.needSquash ||
          (input.squashDetect.needSquash &&
           ROB::isOlder(brRobTag, input.squashDetect.SquashTag))) {
        bru.valid = true;
        bru.pc = pcFrom;
        bru.taken = pcResult != pcFrom + 4;
        bru.target = pcResult;
        const uint8_t cid = input.ROBModule.getCkptId(
            ((brRobTag) & 0x3F));
        bru.ghr = bpCkpt[cid].GHR_snapshot;
        bru.meta = dir.tmeta[cid];
      }
    }
  }
  const auto& cdbOut = input.cdbOut;
  if (cdbOut.valid && cdbOut.isControl && !input.ROBModule.isEmpty() &&
      !ROB::isOlder(cdbOut.robTag, input.ROBModule.getHead())) {
    auto robIdx = ((cdbOut.robTag) & 0x3F);
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
      cdb.target = static_cast<int32_t>(pc);
      cdb.ghr = bpCkpt[input.ROBModule.getCkptId(robIdx)].GHR_snapshot;
      cdb.cond = false;
      cdb.isCall = input.ROBModule.isCall(robIdx);
      cdb.isRet = input.ROBModule.isRet(robIdx);
    }
  }

  auto apply = [&](const Cand &c) {
    if (c.cond)
      CPUstate.BPUModule.update(c.pc, c.taken, c.target, c.ghr, c.meta);
    else
      CPUstate.BPUModule.updateJump(c.pc, c.target, c.isCall, c.isRet,
                                    c.isIndirect);
  };
  if (bru.valid)
    apply(bru);
  if (cdb.valid)
    apply(cdb);

  const auto &fd = input.fetchDecision;
  if (fd.valid) {
    CPUstate.BPUModule.bpCkpt[fd.ckptId] = snapshotCheckPoint();
    CPUstate.BPUModule.dir.tmeta[fd.ckptId] = fd.meta;
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
      CPUstate.BPUModule.tgt.BTB[BTB_index].isCall = fi.isCall;
      CPUstate.BPUModule.tgt.BTB[BTB_index].isRet = false;
      if (fi.jalTargetValid)
        CPUstate.BPUModule.tgt.BTB[BTB_index].target = fi.jalTarget;
      else
        CPUstate.BPUModule.tgt.BTB[BTB_index].isIndirect = true;
    }
    if (fi.isRet) {
      auto BTB_index = (fi.pc >> 2) & (BTB_CAP - 1);
      CPUstate.BPUModule.tgt.BTB[BTB_index].actualPC = fi.pc;
      CPUstate.BPUModule.tgt.BTB[BTB_index].valid = true;
      CPUstate.BPUModule.tgt.BTB[BTB_index].unconditional = true;
      CPUstate.BPUModule.tgt.BTB[BTB_index].isCall = false;
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
