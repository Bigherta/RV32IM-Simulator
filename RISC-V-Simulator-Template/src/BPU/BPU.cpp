#include "../include/BPU.hpp"
#include "../include/ROB.hpp"
#include <cassert>
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

// ---- 周期初快照读接口（lazy 读 _M_old，两路训练共享同一快照 =
//      硬件"周期初采样"，无旁路）----
struct Snap {
  const BPUInner *r;
  // Reset-value presentation: the boot commit lands at the end of cycle 0,
  // but the cycle-0 prediction already reads these tables -- present the
  // constructor-boot constants (main tree: t0=1, useAltOnNa=0b1000) until
  // bootDone commits, so the first functional read sees reset state.
  uint32_t t0(uint32_t i) const {
    return static_cast<bool>(r->bootDone) ? static_cast<uint32_t>(r->dir.t0[i])
                                          : 1u;
  }
  uint32_t LHT(uint32_t i) const {
    return static_cast<uint32_t>(r->dir.LHT[i]);
  }
  uint32_t tnValid(int t, uint32_t i) const {
    return static_cast<uint32_t>(r->dir.tn[t][i].valid);
  }
  uint32_t tnTag(int t, uint32_t i) const {
    return static_cast<uint32_t>(r->dir.tn[t][i].tag);
  }
  uint32_t tnCtr(int t, uint32_t i) const {
    return static_cast<uint32_t>(r->dir.tn[t][i].ctr);
  }
  uint32_t tnU(int t, uint32_t i) const {
    return static_cast<uint32_t>(r->dir.tn[t][i].u);
  }
  uint32_t useAltOnNa(uint32_t i) const {
    return static_cast<bool>(r->bootDone)
               ? static_cast<uint32_t>(r->dir.useAltOnNa[i])
               : 8u;
  }
  bool condSeen(uint32_t i) const {
    return static_cast<bool>(r->tgt.condSeen[i]);
  }
  uint32_t BHT(uint32_t i) const {
    return static_cast<uint32_t>(r->tgt.BHT[i]);
  }
  bool BTBValid(uint32_t i) const {
    return static_cast<bool>(r->tgt.BTB[i].valid);
  }
  uint32_t BTBActualPC(uint32_t i) const {
    return static_cast<uint32_t>(r->tgt.BTB[i].actualPC);
  }
  uint32_t BTBTarget(uint32_t i) const {
    return static_cast<uint32_t>(r->tgt.BTB[i].target);
  }
  bool BTBUncond(uint32_t i) const {
    return static_cast<bool>(r->tgt.BTB[i].unconditional);
  }
  bool BTBCall(uint32_t i) const {
    return static_cast<bool>(r->tgt.BTB[i].isCall);
  }
  bool BTBRet(uint32_t i) const {
    return static_cast<bool>(r->tgt.BTB[i].isRet);
  }
  bool BTBIndirect(uint32_t i) const {
    return static_cast<bool>(r->tgt.BTB[i].isIndirect);
  }
  bool TargetValid(uint32_t i) const {
    return static_cast<bool>(r->tgt.TargetValid[i]);
  }
  uint32_t TargetCache(uint32_t i) const {
    return static_cast<uint32_t>(r->tgt.TargetCache[i]);
  }
  uint64_t ghr() const {
    return (static_cast<uint64_t>(static_cast<uint32_t>(r->dir.GHR_1)) << 32) |
           static_cast<uint32_t>(r->dir.GHR_2);
  }
};

// ---- 训练请求（work() 顶部一次性译码）----
struct TrainReq {
  bool valid = false;
  bool isJump = false; // updateJump vs update
  bool taken = false;
  bool isCall = false;
  bool isRet = false;
  bool isIndirect = false; // main-tree Cand never sets it on either port
  uint32_t pc = 0;
  uint32_t target = 0;
  uint64_t ghr = 0;
  TAGESCMeta meta{};
};

// ---- 本拍写意图（固定容量数组 + count，禁止每拍堆分配）----
enum TabKind : uint8_t {
  T_T0, T_LHT, T_TN_V, T_TN_TAG, T_TN_CTR, T_TN_U, T_UA, T_COND,
  T_BTB_APC, T_BTB_TGT, T_BTB_V, T_BTB_UN, T_BTB_CALL, T_BTB_RET, T_BTB_IND,
  T_BHT, T_TC, T_TCV,
};
struct TabEntry {
  uint8_t kind;
  uint8_t bank; // Tn table index (else 0)
  uint16_t idx;
  uint32_t val;
};
struct Plan {
  TabEntry tab[64];
  uint32_t nTab = 0;
  bool ghr_we = false; // BRU 口专用（投机 GHR）；CDB 口恒 false
  uint64_t ghr_val = 0;
  uint32_t lfsr_steps = 0;   // 本端口触发了几次 LFSR 步进
  uint32_t bankTick_steps = 0; // 本端口 update 次数（bankTickCtr 累加）
  void put(uint8_t kind, uint32_t idx, uint32_t val, uint8_t bank = 0) {
    if (nTab >= 64)
      return;
    tab[nTab++] = {kind, bank, static_cast<uint16_t>(idx), val};
  }
};

// ---- update：纯函数，读 snap、写 plan，零 `<=`。BRU 口可填 ghr/ras；
//      CDB 口作为 commit 只改表（ghr_we 恒 false）。----
Plan updatePlan(const Snap &s, const TrainReq &req) {
  Plan p;
  p.bankTick_steps = 1;
  const uint32_t p2 = req.pc >> 2;
  const uint64_t gh = req.ghr;
  uint32_t idx[TAGE_NTABLES] = {};
  uint8_t tags[TAGE_NTABLES] = {};
  bool hit[TAGE_NTABLES] = {};
  for (int i = 0; i < TAGE_NTABLES; ++i) {
    const int h = TAGE_HIST[i];
    idx[i] = (refoldView(gh, h, TAGE_IDX_BIT) ^
              (p2 & ((1u << TAGE_IDX_BIT) - 1))) &
             ((1u << TAGE_IDX_BIT) - 1);
    tags[i] = static_cast<uint8_t>((refoldView(gh, h, TAGE_TAG_BIT) ^
                                    refoldView(gh, h, TAGE_TAG_BIT - 1) ^
                                    (p2 & ((1u << TAGE_TAG_BIT) - 1))) &
                                   ((1u << TAGE_TAG_BIT) - 1));
    hit[i] = s.tnValid(i, idx[i]) && s.tnTag(i, idx[i]) == tags[i];
  }

  int prov = req.meta.provValid ? static_cast<int>(req.meta.provIdx) : -1;
  p.put(T_COND, p2 & (CONDSEEN_CAP - 1), 1);
  const uint32_t lhtIdx = p2 & (LHT_CAP - 1);
  const uint32_t t0index = (p2 ^ s.LHT(lhtIdx)) & (T0_CAP - 1);
  uint32_t t0v = s.t0(t0index);
  if (req.taken) {
    if (t0v < 3)
      ++t0v;
  } else {
    if (t0v > 0)
      --t0v;
  }
  p.put(T_T0, t0index, t0v);
  p.put(T_LHT, lhtIdx, ((s.LHT(lhtIdx) << 1) | (req.taken ? 1 : 0)) & 0xFFF);

  bool tageCorrect = (req.meta.tagePred == req.taken);
  if (prov >= 0 && hit[prov]) {
    uint32_t ctr = s.tnCtr(prov, idx[prov]);
    if (req.taken) {
      if (ctr < 7)
        ++ctr;
    } else {
      if (ctr > 0)
        --ctr;
    }
    p.put(T_TN_CTR, idx[prov], ctr, static_cast<uint8_t>(prov));
    uint32_t u = s.tnU(prov, idx[prov]);
    if (tageCorrect && req.meta.altPred != req.taken) {
      if (u < 3)
        ++u;
    } else if (!tageCorrect) {
      if (u > 0)
        --u;
    }
    p.put(T_TN_U, idx[prov], u, static_cast<uint8_t>(prov));
  }

  if (prov >= 0 && (req.meta.provCtr == 3 || req.meta.provCtr == 4)) {
    uint32_t ua = s.useAltOnNa(p2 & 127);
    if (req.meta.altPred == req.taken && req.meta.tagePred != req.taken) {
      if (ua < 15)
        ++ua;
    } else if (req.meta.altPred != req.taken && req.meta.tagePred == req.taken) {
      if (ua > 0)
        --ua;
    }
    p.put(T_UA, p2 & 127, ua);
  }

  const bool provConfident =
      prov >= 0 && (req.meta.provCtr <= 1 || req.meta.provCtr >= 6);
  if (!tageCorrect && !(req.meta.altPred == req.taken && provConfident)) {
    const int start = prov + 1;
    bool allocated = false;
    uint8_t l = static_cast<uint8_t>(static_cast<uint32_t>(s.r->dir.lfsr));
    l = static_cast<uint8_t>((l & 1) ? ((l >> 1) ^ LFSR_TAPS) : (l >> 1));
    if (l == 0)
      l = LFSR_SEED;
    p.lfsr_steps = 1;
    // LFSR 步进一次；victim 选择用步进后的值（与 main tree 一致：一次 update
    // 内只步进一次）
    uint8_t lp = l;
    for (int k = 0; k < TAGE_NTABLES && !allocated; ++k) {
      // &(N-1), NOT modulo: TAGE_NTABLES is a power of two (guarded by the
      // static_assert next to its definition). A `%` here would put a real
      // divider on the mispredict-allocation path; the assert turns any
      // non-power-of-two table count into a compile error instead.
      int i = start + ((lp >> (k << 1)) & (TAGE_NTABLES - 1));
      if (i < 0)
        i = 0;
      if (i >= TAGE_NTABLES)
        continue;
      if (!hit[i] || s.tnU(i, idx[i]) == 0) {
        uint32_t ctrv = req.taken ? 4 : 3;
        p.put(T_TN_V, idx[i], 1, static_cast<uint8_t>(i));
        p.put(T_TN_TAG, idx[i], tags[i], static_cast<uint8_t>(i));
        p.put(T_TN_CTR, idx[i], ctrv, static_cast<uint8_t>(i));
        p.put(T_TN_U, idx[i], 0, static_cast<uint8_t>(i));
        allocated = true;
      }
    }
    if (!allocated && start < TAGE_NTABLES) {
      for (int i = start; i < TAGE_NTABLES; ++i) {
        uint32_t u = s.tnU(i, idx[i]);
        if (u > 0)
          --u;
        p.put(T_TN_U, idx[i], u, static_cast<uint8_t>(i));
      }
    }
  }

  auto BTB_index = p2 & (BTB_CAP - 1);
  if (req.taken) {
    p.put(T_BTB_APC, BTB_index, req.pc);
    p.put(T_BTB_TGT, BTB_index, req.target);
    p.put(T_BTB_V, BTB_index, 1);
    p.put(T_BTB_UN, BTB_index, 0);
    p.put(T_BTB_CALL, BTB_index, 0);
    p.put(T_BTB_RET, BTB_index, 0);
    p.put(T_BTB_IND, BTB_index, 0);
  }
  uint32_t bhr = s.BHT(p2 & (BHT_CAP - 1));
  p.put(T_BHT, p2 & (BHT_CAP - 1),
        ((bhr << 1) | (req.taken ? 1 : 0)) & 0xFF);
  return p;
}

Plan updateJumpPlan(const Snap &s, const TrainReq &req) {
  Plan p;
  p.bankTick_steps = 0; // 无条件跳转不推进 bankTickCtr（main tree 语义）
  const uint32_t p2 = req.pc >> 2;
  auto BTB_index = p2 & (BTB_CAP - 1);
  p.put(T_BTB_APC, BTB_index, req.pc);
  p.put(T_BTB_TGT, BTB_index, req.target);
  p.put(T_BTB_V, BTB_index, 1);
  p.put(T_BTB_UN, BTB_index, 1);
  p.put(T_BTB_CALL, BTB_index, req.isCall ? 1 : 0);
  p.put(T_BTB_RET, BTB_index, req.isRet ? 1 : 0);
  // main tree writes the isIndirect PARAM (the CDB candidate never sets it,
  // so committed jumps always train ind=0); the hardcoded 1 diverged hanoi.
  p.put(T_BTB_IND, BTB_index, req.isIndirect ? 1u : 0u);

  const uint32_t bhr = s.BHT(p2 & (BHT_CAP - 1));
  // main tree gates Target-Cache training on isIndirect too (never set on the
  // CDB candidate -> never trained); verbatim equivalence.
  if (req.isIndirect && req.isCall == false && req.isRet == false) { // true indirect
    const uint32_t tcHash = (p2 ^ bhr) & (TARGETCACHE_CAP - 1);
    p.put(T_TC, tcHash, req.target);
    p.put(T_TCV, tcHash, 1);
  }
  p.put(T_BHT, p2 & (BHT_CAP - 1), ((bhr << 1) | 1) & 0xFF);
  return p;
}
} // namespace

PredictInfo BPU::predict(int32_t pc) const {
  Snap s(this);
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  const uint64_t ghr = s.ghr();
  const uint32_t lhtIdx = p2 & (LHT_CAP - 1);
  const uint32_t t0index = (p2 ^ s.LHT(lhtIdx)) & (T0_CAP - 1);
  const bool basePred = s.t0(t0index) >= 2;
  bool hit[TAGE_NTABLES] = {};
  uint32_t idx[TAGE_NTABLES] = {};
  uint8_t tags[TAGE_NTABLES] = {};
  for (int i = 0; i < TAGE_NTABLES; ++i) {
    const int h = TAGE_HIST[i];
    idx[i] = (refoldView(ghr, h, TAGE_IDX_BIT) ^
              (p2 & ((1u << TAGE_IDX_BIT) - 1))) &
             ((1u << TAGE_IDX_BIT) - 1);
    tags[i] = static_cast<uint8_t>((refoldView(ghr, h, TAGE_TAG_BIT) ^
                                    refoldView(ghr, h, TAGE_TAG_BIT - 1) ^
                                    (p2 & ((1u << TAGE_TAG_BIT) - 1))) &
                                   ((1u << TAGE_TAG_BIT) - 1));
    hit[i] = s.tnValid(i, idx[i]) && s.tnTag(i, idx[i]) == tags[i];
  }
  int prov = -1, alt = -1;
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
    altPred = s.tnCtr(alt, idx[alt]) >= 4;
  else if (prov >= 0)
    altPred = basePred;
  bool tagePred = basePred;
  uint8_t provCtr = 0, provU = 0;
  bool provValid = false;
  if (prov >= 0) {
    provValid = true;
    provCtr = static_cast<uint8_t>(s.tnCtr(prov, idx[prov]));
    provU = static_cast<uint8_t>(s.tnU(prov, idx[prov]));
    const bool weak = (provCtr == 3 || provCtr == 4);
    const bool useAlt = s.useAltOnNa(p2 & 127) >= 8;
    tagePred = (weak && useAlt) ? altPred : (provCtr >= 4);
  }
  bool taken = tagePred;
  const auto BTB_index = p2 & (BTB_CAP - 1);
  bool btbHit =
      s.BTBValid(BTB_index) && s.BTBActualPC(BTB_index) == static_cast<uint32_t>(pc);
  if (btbHit && s.BTBUncond(BTB_index))
    taken = true;
  const uint32_t bhr = s.BHT(p2 & (BHT_CAP - 1));
  const uint32_t tcHash = (p2 ^ bhr) & (TARGETCACHE_CAP - 1);
  const bool tcUsable = btbHit && s.BTBIndirect(BTB_index) &&
                        !s.BTBCall(BTB_index) && !s.BTBRet(BTB_index) &&
                        s.TargetValid(tcHash);
  // RET with empty RAS: don't use BTB target 0, treat as not taken (wild fetch fix)
  bool isRet = s.BTBRet(BTB_index);
  bool rasEmpty = static_cast<uint32_t>(tgt.RAS_top) == 0;
  if (isRet && rasEmpty) {
    btbHit = false;
    taken = false;
  }
  int32_t predictPC = pc + 4;
  if (taken && btbHit) {
    if (isRet && static_cast<uint32_t>(tgt.RAS_top) > 0)
      predictPC = static_cast<int32_t>(
          static_cast<uint32_t>(tgt.RAS[(static_cast<uint32_t>(tgt.RAS_top) - 1) &
                                        (RAS_CAP - 1)].retPC));
    else if (tcUsable)
      predictPC = static_cast<int32_t>(s.TargetCache(tcHash));
    else
      predictPC = static_cast<int32_t>(s.BTBTarget(BTB_index));
  }
  PredictInfo out{taken, predictPC};
  out.btbHit = btbHit;
  out.unconditional = btbHit && s.BTBUncond(BTB_index);
  out.meta.provValid = provValid;
  out.meta.provIdx = provValid ? static_cast<uint8_t>(prov) : 0;
  out.meta.provCtr = provCtr;
  out.meta.provU = provU;
  out.meta.altPred = altPred;
  out.meta.tagePred = tagePred;
  out.meta.baseCnt = static_cast<uint8_t>(s.t0(t0index));
  out.condSeen = s.condSeen(p2 & (CONDSEEN_CAP - 1));
  return out;
}

// Fetch-direction guard, verbatim from the retired FetchDecision::build
// (squash source is flushArbiter.needSquash, the same selectOldest() the
// former arbitResult() bridge read).
bool BPU::fetchAllowed() const {
  return !static_cast<bool>(fetchCtx.squashNeed) &&
         !static_cast<bool>(fetchCtx.haltFetched) &&
         !static_cast<bool>(fetchCtx.fqFull) &&
         !static_cast<bool>(fetchCtx.imemReqFull);
}

// ---- fetch-stage prediction bundle (retired FetchDecision::build, now the
// producer-owned combinational cloud; hardware = NPC/redirect logic inside
// the predictor). Single-point evaluation: each mid net calls predict()
// once per cycle; period-freeze (every table read lands on _M_old) makes
// any re-evaluation bit-identical, so two packed nets reproduce the former
// single build() call. Guarded cycles 0-fill every field, bit-identical to
// the retired default-initialized FetchDecision{}.
void BPU::wire_output() {
  mid.predPC = [this]() -> uint32_t {
    if (!fetchAllowed())
      return 0u;
    const PredictInfo p =
        predict(static_cast<int32_t>(static_cast<uint32_t>(fetchCtx.pc)));
    return p.taken ? static_cast<uint32_t>(p.predictPC)
                   : static_cast<uint32_t>(fetchCtx.pc) + 4u;
  };
  mid.packed = [this]() -> uint32_t {
    if (!fetchAllowed())
      return 0u;
    const PredictInfo p =
        predict(static_cast<int32_t>(static_cast<uint32_t>(fetchCtx.pc)));
    // shift/shiftValue branch structure verbatim from build():
    // btbHit wins over condSeen; unconditional forces shiftValue.
    const bool shift = p.btbHit || p.condSeen;
    const bool shiftValue =
        p.btbHit ? (p.unconditional ? true : p.taken)
                 : (p.condSeen ? p.taken : false);
    // ckptId occupies packed bits [7:2] (shift/shiftValue own bits 0/1),
    // so it must be shifted into place before the flag bits are ORed in.
    uint32_t v = (static_cast<uint32_t>(getNextCkptId()) & 0x3Fu) << 2;
    if (shift)
      v |= 1u << 0;
    if (shiftValue)
      v |= 1u << 1;
    if (p.meta.provValid)
      v |= 1u << 8;
    v |= (static_cast<uint32_t>(p.meta.provIdx) & 0x3u) << 9;
    v |= (static_cast<uint32_t>(p.meta.provCtr) & 0x7u) << 11;
    v |= (static_cast<uint32_t>(p.meta.provU) & 0x3u) << 14;
    if (p.meta.altPred)
      v |= 1u << 16;
    if (p.meta.tagePred)
      v |= 1u << 17;
    v |= (static_cast<uint32_t>(p.meta.baseCnt) & 0x3u) << 18;
    return v;
  };
  fetchOut.valid = [this]() -> uint32_t { return fetchAllowed() ? 1u : 0u; };
  fetchOut.pc = [this]() -> uint32_t {
    return fetchAllowed() ? static_cast<uint32_t>(fetchCtx.pc) : 0u;
  };
  fetchOut.predictedPC = [this]() -> uint32_t {
    return static_cast<uint32_t>(mid.predPC);
  };
  fetchOut.shift = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 0) & 0x1u;
  };
  fetchOut.shiftValue = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 1) & 0x1u;
  };
  fetchOut.ckptId = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 2) & 0x3Fu;
  };
  fetchOut.provValid = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 8) & 0x1u;
  };
  fetchOut.provIdx = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 9) & 0x3u;
  };
  fetchOut.provCtr = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 11) & 0x7u;
  };
  fetchOut.provU = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 14) & 0x3u;
  };
  fetchOut.altPred = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 16) & 0x1u;
  };
  fetchOut.tagePred = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 17) & 0x1u;
  };
  fetchOut.baseCnt = [this]() -> uint32_t {
    return (static_cast<uint32_t>(mid.packed) >> 18) & 0x3u;
  };
}

BPUSnapshot BPU::snapshotCheckPoint() const {
  BPUSnapshot s;
  s.GHR_snapshot = getGHR();
  s.alignHead = static_cast<uint32_t>(tgt.alignHead);
  s.alignTail = static_cast<uint32_t>(tgt.alignTail);
  s.RAS_top = static_cast<uint32_t>(tgt.RAS_top);
  return s;
}

void BPU::work() {
  // Cycle-0 boot: t0/useAltOnNa need non-zero init. Runs in parallel with the
  // normal logic (no if(!boot) exclusion): cycle 0 has ROB/BRU empty so no
  // training candidate can write t0/useAltOnNa -- the build-assert probe is
  // the runtime proof of this structural guarantee.
  bool boot = !static_cast<bool>(bootDone);
  if (boot) {
    for (int i = 0; i < T0_CAP; ++i)
      dir.t0[i] <= 1;
    for (int i = 0; i < 128; ++i)
      dir.useAltOnNa[i] <= 8;
    dir.lfsr <= LFSR_SEED;
    bootDone <= true;
  }

  Snap snap(this);
  bool needSquash = static_cast<bool>(squash.needSquash);
  uint32_t squashTag = static_cast<uint32_t>(squash.SquashTag);
  uint32_t squashCkpt = static_cast<uint32_t>(squash.SquashCkpt);

  // ---- decode both train requests (once per cycle) ----
  TrainReq trBru, trCdb;
  if (static_cast<bool>(bru.isBRUEmpty) == false) {
    auto brRobTag = static_cast<uint32_t>(bru.bruHeadRobTag);
    auto pcResult = static_cast<uint32_t>(bru.bruHeadPCResult);
    auto pcFrom = static_cast<uint32_t>(bru.bruHeadPCFrom);
    ++branchTotal;
    bool correct = pcResult ==
                   static_cast<uint32_t>(rob.robPredictPC[brRobTag & 0x3F]);
    if (correct)
      ++branchCorrect;
    if (!needSquash || ROB::isOlder(brRobTag, squashTag)) {
      trBru.valid = true;
      trBru.isJump = false;
      trBru.pc = pcFrom;
      trBru.taken = pcResult != pcFrom + 4;
      trBru.target = pcResult;
      auto cid = static_cast<uint32_t>(rob.robCkptId[brRobTag & 0x3F]);
      // bpCkpt/tmeta single-source (fetch allocation) writes, squash reads only; same-cycle
      // alloc+rollback use distinct ckptIds
      // (alloc id always newer than rollback id, ring distance >= 1) -- no race.
      trBru.ghr =
          (static_cast<uint64_t>(static_cast<uint32_t>(bpCkpt[cid].GHR_1)) << 32) |
          static_cast<uint32_t>(bpCkpt[cid].GHR_2);
      trBru.meta.provValid = static_cast<bool>(dir.tmeta[cid].provValid);
      trBru.meta.provIdx = static_cast<uint8_t>(static_cast<uint32_t>(dir.tmeta[cid].provIdx));
      trBru.meta.provCtr = static_cast<uint8_t>(static_cast<uint32_t>(dir.tmeta[cid].provCtr));
      trBru.meta.provU = static_cast<uint8_t>(static_cast<uint32_t>(dir.tmeta[cid].provU));
      trBru.meta.altPred = static_cast<bool>(dir.tmeta[cid].altPred);
      trBru.meta.tagePred = static_cast<bool>(dir.tmeta[cid].tagePred);
      trBru.meta.baseCnt = static_cast<uint8_t>(static_cast<uint32_t>(dir.tmeta[cid].baseCnt));
    }
  }
  if (static_cast<bool>(cdb.cdbValid) && static_cast<bool>(cdb.cdbIsControl) &&
      static_cast<bool>(rob.isROBEmpty) == false &&
      !ROB::isOlder(static_cast<uint32_t>(cdb.cdbRobTag),
                    static_cast<uint32_t>(rob.robHeadTag))) {
    auto robIdx = static_cast<uint32_t>(cdb.cdbRobTag) & 0x3F;
    auto pc = static_cast<uint32_t>(cdb.cdbValue);
    if (!needSquash ||
        ROB::isOlder(static_cast<uint32_t>(cdb.cdbRobTag), squashTag)) {
      ++branchTotal;
      bool correct = pc == static_cast<uint32_t>(rob.robPredictPC[robIdx]);
      if (correct)
        ++branchCorrect;
      trCdb.valid = true;
      trCdb.isJump = true;
      trCdb.pc = static_cast<uint32_t>(rob.robPC[robIdx]);
      trCdb.target = pc;
      trCdb.isCall = static_cast<bool>(rob.robIsCall[robIdx]);
      trCdb.isRet = static_cast<bool>(rob.robIsRet[robIdx]);
    }
  }


  // ---- two plans from the same snap, no bypass between ports ----
  Plan p_bru, p_cdb;
  if (trBru.valid)
    p_bru = trBru.isJump ? updateJumpPlan(snap, trBru) : updatePlan(snap, trBru);
  if (trCdb.valid)
    p_cdb = updateJumpPlan(snap, trCdb);
  // CDB port is commit: ghr_we stays false (neither plan sets it)

  // ---- fetch allocation (BRU-port speculative: bpCkpt/tmeta/GHR shift/nextCkptId) ----
  uint64_t ghrLocal = snap.ghr();
  uint32_t nextCkpt = static_cast<uint32_t>(nextCkptId);
  if (static_cast<bool>(fetchOut.valid)) {
    auto ck = static_cast<uint32_t>(fetchOut.ckptId);
    BPUSnapshot s = snapshotCheckPoint();
    bpCkpt[ck].GHR_1 <= static_cast<uint32_t>(s.GHR_snapshot >> 32);
    bpCkpt[ck].GHR_2 <= static_cast<uint32_t>(s.GHR_snapshot);
    bpCkpt[ck].alignHead <= static_cast<uint32_t>(s.alignHead);
    bpCkpt[ck].alignTail <= static_cast<uint32_t>(s.alignTail);
    bpCkpt[ck].RAS_top <= static_cast<uint32_t>(s.RAS_top);
    dir.tmeta[ck].provValid <= static_cast<bool>(fetchOut.provValid);
    dir.tmeta[ck].provIdx <= static_cast<uint32_t>(fetchOut.provIdx);
    dir.tmeta[ck].provCtr <= static_cast<uint32_t>(fetchOut.provCtr);
    dir.tmeta[ck].provU <= static_cast<uint32_t>(fetchOut.provU);
    dir.tmeta[ck].altPred <= static_cast<bool>(fetchOut.altPred);
    dir.tmeta[ck].tagePred <= static_cast<bool>(fetchOut.tagePred);
    dir.tmeta[ck].baseCnt <= static_cast<uint32_t>(fetchOut.baseCnt);
    if (static_cast<bool>(fetchOut.shift))
      ghrLocal = (ghrLocal << 1) |
                 (static_cast<bool>(fetchOut.shiftValue) ? 1u : 0u);
    nextCkpt = (ck + 1) & (CKPT_CAP - 1);
  }

  // ---- RAS / alignQueue local mirror (BRU-port fetchInfo + squash rewind override) ----
  uint32_t rasTop = static_cast<uint32_t>(tgt.RAS_top);
  uint32_t alignTail = static_cast<uint32_t>(tgt.alignTail);
  uint32_t alignHead = static_cast<uint32_t>(tgt.alignHead);
  // Pre-fi-block base (main-tree semantics): the squash rewind computes its
  // replay range from the comb-snapshot alignTail, so journal entries written
  // by THIS tick's fetchInfo block are NOT undone by the restore.
  uint32_t alignTailPreFi = alignTail;
  uint32_t rasRetPC[RAS_CAP], rasTimes[RAS_CAP];
  uint32_t alAddr[ALIGNQ_CAP], alIndex[ALIGNQ_CAP], alTimes[ALIGNQ_CAP];
  for (int i = 0; i < RAS_CAP; ++i) {
    rasRetPC[i] = static_cast<uint32_t>(tgt.RAS[i].retPC);
    rasTimes[i] = static_cast<uint32_t>(tgt.RAS[i].times);
  }
  for (int i = 0; i < ALIGNQ_CAP; ++i) {
    alAddr[i] = static_cast<uint32_t>(tgt.alignQueue[i].addr);
    alIndex[i] = static_cast<uint32_t>(tgt.alignQueue[i].index);
    alTimes[i] = static_cast<uint32_t>(tgt.alignQueue[i].times);
  }

  Plan p_fi;
  if (static_cast<bool>(fetchInfo.FetchValid)) {
    const uint32_t ra = static_cast<uint32_t>(fetchInfo.FetchPC) + 4;
    if (static_cast<bool>(fetchInfo.isFetchCall)) {
      uint32_t topIdx = rasTop & (RAS_CAP - 1);
      if (rasTop > 0 && rasRetPC[(rasTop - 1) & (RAS_CAP - 1)] == ra) {
        alAddr[alignTail & (ALIGNQ_CAP - 1)] =
            rasRetPC[(rasTop - 1) & (RAS_CAP - 1)];
        alIndex[alignTail & (ALIGNQ_CAP - 1)] = (rasTop - 1) & (RAS_CAP - 1);
        alTimes[alignTail & (ALIGNQ_CAP - 1)] =
            rasTimes[(rasTop - 1) & (RAS_CAP - 1)];
        ++alignTail;
        ++rasTimes[(rasTop - 1) & (RAS_CAP - 1)];
      } else {
        rasRetPC[topIdx] = ra;
        rasTimes[topIdx] = 1;
        ++rasTop;
      }
    } else if (static_cast<bool>(fetchInfo.isFetchRet) && rasTop > 0) {
      uint32_t topIdx = (rasTop - 1) & (RAS_CAP - 1);
      alAddr[alignTail & (ALIGNQ_CAP - 1)] = rasRetPC[topIdx];
      alIndex[alignTail & (ALIGNQ_CAP - 1)] = topIdx;
      alTimes[alignTail & (ALIGNQ_CAP - 1)] = rasTimes[topIdx];
      ++alignTail;
      if (rasTimes[topIdx] > 1)
        --rasTimes[topIdx];
      else
        --rasTop;
    }
    // Early training (BRU-port fetch side; same-slot conflicts arbitrated in commit, fetch highest)
    if (static_cast<bool>(fetchInfo.isFetchCall) ||
        (!static_cast<bool>(fetchInfo.isFetchCall) &&
         !static_cast<bool>(fetchInfo.isFetchRet))) {
      auto BTB_index =
          (static_cast<uint32_t>(fetchInfo.FetchPC) >> 2) & (BTB_CAP - 1);
      p_fi.put(T_BTB_APC, BTB_index, static_cast<uint32_t>(fetchInfo.FetchPC));
      p_fi.put(T_BTB_V, BTB_index, 1);
      p_fi.put(T_BTB_UN, BTB_index, 1);
      p_fi.put(T_BTB_CALL, BTB_index,
               static_cast<bool>(fetchInfo.isFetchCall) ? 1 : 0);
      p_fi.put(T_BTB_RET, BTB_index, 0);
      if (static_cast<bool>(fetchInfo.FetchJALTargetValid))
        p_fi.put(T_BTB_TGT, BTB_index,
                 static_cast<uint32_t>(fetchInfo.FetchJALTarget));
      else
        p_fi.put(T_BTB_IND, BTB_index, 1);
    }
    if (static_cast<bool>(fetchInfo.isFetchRet)) {
      auto BTB_index =
          (static_cast<uint32_t>(fetchInfo.FetchPC) >> 2) & (BTB_CAP - 1);
      p_fi.put(T_BTB_APC, BTB_index, static_cast<uint32_t>(fetchInfo.FetchPC));
      p_fi.put(T_BTB_V, BTB_index, 1);
      p_fi.put(T_BTB_UN, BTB_index, 1);
      p_fi.put(T_BTB_CALL, BTB_index, 0);
      p_fi.put(T_BTB_RET, BTB_index, 1);
    }
  }

  // Checkpoint allocation and squash rollback never happen in the same cycle:
  // the fetch-allocated ckptId is always newer than the rollback ckptId
  // (ring distance >= 1), so bpCkpt[ck] reads never race the same-slot write.
  assert(!(static_cast<bool>(fetchOut.valid) && needSquash));
  // ---- squash restore (highest priority over all speculative state) ----
  bool didSquash = false;
  if (needSquash) {
    didSquash = true;
    uint32_t ck = squashCkpt & (CKPT_CAP - 1);
    uint64_t ckptGHR =
        (static_cast<uint64_t>(static_cast<uint32_t>(bpCkpt[ck].GHR_1)) << 32) |
        static_cast<uint32_t>(bpCkpt[ck].GHR_2);
    uint32_t ckptAlignTail = static_cast<uint32_t>(bpCkpt[ck].alignTail);
    uint32_t ckptAlignHead = static_cast<uint32_t>(bpCkpt[ck].alignHead);
    uint32_t ckptRasTop = static_cast<uint32_t>(bpCkpt[ck].RAS_top);
    uint32_t curTail = alignTailPreFi;
    uint32_t base = ckptAlignTail;
    // mod-256 distance (alignTail is an 8-bit ring counter): the main tree
    // computes this in uint8_t, so 0-255 wraps to 1 -- a uint32 subtraction
    // underflows and replays garbage journal entries.
    uint32_t dist = (curTail - base) & 0xFF;
    for (int k = 0; k < ALIGNQ_CAP; ++k) {
      if (static_cast<uint32_t>(k) >= dist)
        continue;
      uint32_t pos = curTail - 1 - static_cast<uint32_t>(k);
      uint32_t idx = alIndex[pos & (ALIGNQ_CAP - 1)] & (RAS_CAP - 1);
      rasRetPC[idx] = alAddr[pos & (ALIGNQ_CAP - 1)];
      rasTimes[idx] = alTimes[pos & (ALIGNQ_CAP - 1)];
    }
    ghrLocal = ckptGHR;
    alignTail = ckptAlignTail;
    alignHead = ckptAlignHead;
    rasTop = ckptRasTop;
    nextCkpt = (ck + 1) & (CKPT_CAP - 1);
  }

  // ---- commit: resource-typed arbitration, explicit next-state mux ----
  {
    // BHT dual-port collision: the main tree applies bru then cdb
    // sequentially, so the commit write shifts the ALREADY-bru-shifted byte
    // once more -- final value ((bruVal << 1) | 1). Reproduce that as a
    // single combinational din here (non-colliding writes unchanged; fi
    // never writes BHT).
    for (uint32_t qb = 0; qb < p_bru.nTab; ++qb) {
      const auto &eb = p_bru.tab[qb];
      if (eb.kind != T_BHT)
        continue;
      for (uint32_t qc = 0; qc < p_cdb.nTab; ++qc) {
        auto &ec = p_cdb.tab[qc];
        if (ec.kind == T_BHT && ec.idx == eb.idx)
          ec.val = ((eb.val << 1) | 1u) & 0xFFu;
      }
    }
    // bankTickCtr event accumulation (only update advances it; updateJump does not)
    uint32_t btc = static_cast<uint32_t>(dir.bankTickCtr);
    btc += p_bru.bankTick_steps + p_cdb.bankTick_steps;
    bool tick = false;
    if (btc >= BANKTICK_MAX) {
      tick = true;
      btc -= BANKTICK_MAX;
    }

    // Table writes: merge fi > cdb > bru (first-in wins = higher priority holds
    // the slot, lower priority skips duplicates) so every physical Register is
    // assigned at most once per cycle.
    Plan merged;
    auto mergeIn = [&](const Plan &p) {
      for (uint32_t q = 0; q < p.nTab; ++q) {
        const auto &e = p.tab[q];
        bool dup = false;
        for (uint32_t r = 0; r < merged.nTab; ++r)
          if (merged.tab[r].kind == e.kind && merged.tab[r].bank == e.bank &&
              merged.tab[r].idx == e.idx)
            dup = true;
        if (!dup)
          merged.put(e.kind, e.idx, e.val, e.bank);
      }
    };
    mergeIn(p_fi);
    mergeIn(p_cdb);
    mergeIn(p_bru);

    // tn.u: explicit next-state mux with priority tick < bru < cdb.
    // Single write per physical Register. On tick cycles scan the full table
    // (u >>= 1 base), on non-tick cycles only the plan-touched slots are
    // written (enabled write -- no whole-table self-write).
    auto tnUVal = [&](const Plan &p, int t, uint32_t j, bool &hit) -> uint32_t {
      for (uint32_t q = 0; q < p.nTab; ++q)
        if (p.tab[q].kind == T_TN_U && p.tab[q].bank == t && p.tab[q].idx == j) {
          hit = true;
          return p.tab[q].val;
        }
      hit = false;
      return 0;
    };
    if (tick) {
      for (int t = 0; t < TAGE_NTABLES; ++t) {
        for (int j = 0; j < (1 << TAGE_IDX_BIT); ++j) {
          uint32_t nextU = static_cast<uint32_t>(dir.tn[t][j].u) >> 1; // tick base
          bool hb, hc;
          uint32_t vb = tnUVal(p_bru, t, j, hb);
          uint32_t vc = tnUVal(p_cdb, t, j, hc);
          if (hb)
            nextU = vb; // bru din overrides tick decay
          if (hc)
            nextU = vc; // cdb din highest among bru/tick
          dir.tn[t][j].u <= nextU;
        }
      }
    } else {
      // non-tick: only plan-touched u slots
      for (uint32_t q = 0; q < merged.nTab; ++q) {
        const auto &e = merged.tab[q];
        if (e.kind == T_TN_U)
          dir.tn[e.bank][e.idx].u <= e.val;
      }
    }

    // Other tables: single write per plan entry (merged already deduplicated).
    auto apply = [&](const Plan &p) {
      for (uint32_t k = 0; k < p.nTab; ++k) {
        const auto &e = p.tab[k];
        switch (e.kind) {
        case T_T0: dir.t0[e.idx] <= e.val; break;
        case T_LHT: dir.LHT[e.idx] <= e.val; break;
        case T_TN_V: dir.tn[e.bank][e.idx].valid <= e.val; break;
        case T_TN_TAG: dir.tn[e.bank][e.idx].tag <= e.val; break;
        case T_TN_CTR: dir.tn[e.bank][e.idx].ctr <= e.val; break;
        case T_UA: dir.useAltOnNa[e.idx] <= e.val; break;
        case T_COND: tgt.condSeen[e.idx] <= e.val; break;
        case T_BTB_APC: tgt.BTB[e.idx].actualPC <= e.val; break;
        case T_BTB_TGT: tgt.BTB[e.idx].target <= e.val; break;
        case T_BTB_V: tgt.BTB[e.idx].valid <= e.val; break;
        case T_BTB_UN: tgt.BTB[e.idx].unconditional <= e.val; break;
        case T_BTB_CALL: tgt.BTB[e.idx].isCall <= e.val; break;
        case T_BTB_RET: tgt.BTB[e.idx].isRet <= e.val; break;
        case T_BTB_IND: tgt.BTB[e.idx].isIndirect <= e.val; break;
        case T_BHT: tgt.BHT[e.idx] <= e.val; break;
        case T_TC: tgt.TargetCache[e.idx] <= e.val; break;
        case T_TCV: tgt.TargetValid[e.idx] <= e.val; break;
        default: break;
        }
      }
    };
    apply(merged);

    // Scalar event stepping (lfsr: per-port event count, ordered accumulation).
    // Enabled write: only step cycles drive the register -- an unconditional
    // writeback would read _M_old=0 and clobber the cycle-0 boot seed
    // (double-write with the boot block; the seed loss diverged the LFSR
    // victim-pick sequence from the main tree's constructor-initialized
    // lfsr=LFSR_SEED).
    uint32_t lfsrSteps = p_bru.lfsr_steps + p_cdb.lfsr_steps;
    if (lfsrSteps > 0) {
      uint32_t lfsr = static_cast<uint32_t>(dir.lfsr);
      for (uint32_t s = 0; s < lfsrSteps; ++s) {
        lfsr = static_cast<uint8_t>((lfsr & 1) ? ((lfsr >> 1) ^ LFSR_TAPS) : (lfsr >> 1));
        if (lfsr == 0)
          lfsr = LFSR_SEED;
      }
      dir.lfsr <= lfsr;
    }
    dir.bankTickCtr <= btc;
  }

  // Speculative-state writeback (squash > BRU; CDB never participates)
  dir.GHR_1 <= static_cast<uint32_t>(ghrLocal >> 32);
  dir.GHR_2 <= static_cast<uint32_t>(ghrLocal);
  nextCkptId <= nextCkpt;
  tgt.RAS_top <= rasTop;
  tgt.alignHead <= alignHead;
  tgt.alignTail <= alignTail;
  for (int i = 0; i < RAS_CAP; ++i) {
    tgt.RAS[i].retPC <= rasRetPC[i];
    tgt.RAS[i].times <= rasTimes[i];
  }
  for (int i = 0; i < ALIGNQ_CAP; ++i) {
    tgt.alignQueue[i].addr <= alAddr[i];
    tgt.alignQueue[i].index <= alIndex[i];
    tgt.alignQueue[i].times <= alTimes[i];
  }
}