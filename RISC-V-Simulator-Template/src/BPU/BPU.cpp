#include "../include/BPU.hpp"

#include <cstdint>
#include <cstring>

#include "../include/ROB.hpp"

namespace {
// Compile-time folded-history fold (Seznec): XOR the low HIST_LEN bits of GHR
// into FOLD_WIDTH bits, one FOLD_WIDTH-bit chunk at a time. Both the trip count
// and the chunk stride are template parameters, so this unrolls into a pure XOR
// tree with no runtime-bounded loop (audit item C-6), and the chunk count is
// fixed at ceil(H/W) BY CONSTRUCTION.
//
// The literal XOR trees this replaced had dropped the *partial final chunk* for
// (H=48, W=9) -- 48 % 9 = 3, so the last chunk is a 3-bit one -- in two separate
// places (the squash rebuild and updatePlan's allocator), which mis-indexed T4
// and cost 14..7734 cycles across 8 benchmarks. Deriving H and W from the
// constants below makes that class of error unrepresentable; do not reintroduce
// a literal unroll here.
template <int HIST_LEN, int FOLD_WIDTH>
constexpr uint32_t refoldViewT(uint64_t ghr) {
  constexpr uint32_t fmask =
      FOLD_WIDTH >= 32 ? 0xffffffffu : ((1u << FOLD_WIDTH) - 1u);
  ghr &= (uint64_t{1} << HIST_LEN) - 1u;
  uint32_t r = 0;
  for (int s = 0; s < HIST_LEN; s += FOLD_WIDTH) // both bounds are constants
    r ^= static_cast<uint32_t>(ghr >> s) & fmask;
  return r & fmask;
}

// Per-table dispatch: `i` indexes TAGE_NTABLES (a compile-time constant), so
// the switch folds away and every call is a fully unrolled XOR tree.
constexpr uint32_t foldIdx(int i, uint64_t ghr) {
  switch (i) {
  case 0: return refoldViewT<TAGE_HIST[0], TAGE_IDX_BIT>(ghr);
  case 1: return refoldViewT<TAGE_HIST[1], TAGE_IDX_BIT>(ghr);
  case 2: return refoldViewT<TAGE_HIST[2], TAGE_IDX_BIT>(ghr);
  default: return refoldViewT<TAGE_HIST[3], TAGE_IDX_BIT>(ghr);
  }
}
constexpr uint32_t foldTag8(int i, uint64_t ghr) {
  switch (i) {
  case 0: return refoldViewT<TAGE_HIST[0], TAGE_TAG_BIT>(ghr);
  case 1: return refoldViewT<TAGE_HIST[1], TAGE_TAG_BIT>(ghr);
  case 2: return refoldViewT<TAGE_HIST[2], TAGE_TAG_BIT>(ghr);
  default: return refoldViewT<TAGE_HIST[3], TAGE_TAG_BIT>(ghr);
  }
}
constexpr uint32_t foldTag7(int i, uint64_t ghr) {
  switch (i) {
  case 0: return refoldViewT<TAGE_HIST[0], TAGE_TAG_BIT - 1>(ghr);
  case 1: return refoldViewT<TAGE_HIST[1], TAGE_TAG_BIT - 1>(ghr);
  case 2: return refoldViewT<TAGE_HIST[2], TAGE_TAG_BIT - 1>(ghr);
  default: return refoldViewT<TAGE_HIST[3], TAGE_TAG_BIT - 1>(ghr);
  }
}

// Incremental-step wrap amount (H % W) per table, computed by the COMPILER from
// the real constants. A constexpr table means (a) the datapath never sees a
// runtime modulo, and (b) the H % W == 0 rows -- where the wrap term is
// `disc << 0`, i.e. plain `disc` and NOT an absent term -- cannot be
// hand-evaluated wrong (that mistake has been made twice in this file).
constexpr int wrapShiftIdx[TAGE_NTABLES] = {
    TAGE_HIST[0] % TAGE_IDX_BIT, TAGE_HIST[1] % TAGE_IDX_BIT,
    TAGE_HIST[2] % TAGE_IDX_BIT, TAGE_HIST[3] % TAGE_IDX_BIT};
constexpr int wrapShiftTag8[TAGE_NTABLES] = {
    TAGE_HIST[0] % TAGE_TAG_BIT, TAGE_HIST[1] % TAGE_TAG_BIT,
    TAGE_HIST[2] % TAGE_TAG_BIT, TAGE_HIST[3] % TAGE_TAG_BIT};
constexpr int wrapShiftTag7[TAGE_NTABLES] = {
    TAGE_HIST[0] % (TAGE_TAG_BIT - 1), TAGE_HIST[1] % (TAGE_TAG_BIT - 1),
    TAGE_HIST[2] % (TAGE_TAG_BIT - 1), TAGE_HIST[3] % (TAGE_TAG_BIT - 1)};

static_assert(TAGE_NTABLES == 4,
              "foldIdx/foldTag8/foldTag7 dispatch covers exactly 4 TAGE tables");
static_assert(TAGE_IDX_BIT < 32 && TAGE_TAG_BIT < 32 && TAGE_TAG_BIT - 1 < 32,
              "folded-history widths must be < 32 (mask = 1u << W)");
static_assert(TAGE_HIST[TAGE_NTABLES - 1] < 64,
              "HIST must fit the 64-bit GHR window");

struct Snap {
  const BPUInner *st;
  // Reset-value presentation: boot commits at end of cycle 0, but cycle-0
  // prediction already reads these tables -- present boot constants until then.
  uint32_t t0(uint32_t i) const {
    return static_cast<bool>(st->bootDone)
               ? static_cast<uint32_t>(st->dir.t0[i])
               : 1u;
  }
  uint32_t LHT(uint32_t i) const {
    return static_cast<uint32_t>(st->dir.LHT[i]);
  }
  uint32_t tnValid(int t, uint32_t i) const {
    return static_cast<uint32_t>(st->dir.tn[t][i].valid);
  }
  uint32_t tnTag(int t, uint32_t i) const {
    return static_cast<uint32_t>(st->dir.tn[t][i].tag);
  }
  uint32_t tnCtr(int t, uint32_t i) const {
    return static_cast<uint32_t>(st->dir.tn[t][i].ctr);
  }
  uint32_t tnU(int t, uint32_t i) const {
    return static_cast<uint32_t>(st->dir.tn[t][i].u);
  }
  uint32_t useAltOnNa(uint32_t i) const {
    return static_cast<bool>(st->bootDone)
               ? static_cast<uint32_t>(st->dir.useAltOnNa[i])
               : 8u;
  }
  bool condSeen(uint32_t i) const {
    return static_cast<bool>(st->tgt.condSeen[i]);
  }
  uint32_t BHT(uint32_t i) const {
    return static_cast<uint32_t>(st->tgt.BHT[i]);
  }
  bool BTBValid(uint32_t i) const {
    return static_cast<bool>(st->tgt.BTB[i].valid);
  }
  uint32_t BTBActualPC(uint32_t i) const {
    return static_cast<uint32_t>(st->tgt.BTB[i].actualPC);
  }
  uint32_t BTBTarget(uint32_t i) const {
    return static_cast<uint32_t>(st->tgt.BTB[i].target);
  }
  bool BTBUncond(uint32_t i) const {
    return static_cast<bool>(st->tgt.BTB[i].unconditional);
  }
  bool BTBCall(uint32_t i) const {
    return static_cast<bool>(st->tgt.BTB[i].isCall);
  }
  bool BTBRet(uint32_t i) const {
    return static_cast<bool>(st->tgt.BTB[i].isRet);
  }
  bool BTBIndirect(uint32_t i) const {
    return static_cast<bool>(st->tgt.BTB[i].isIndirect);
  }
  bool TargetValid(uint32_t i) const {
    return static_cast<bool>(st->tgt.TargetValid[i]);
  }
  uint32_t TargetCache(uint32_t i) const {
    return static_cast<uint32_t>(st->tgt.TargetCache[i]);
  }
  // Folded-history views (incrementally maintained, see the writeback block)
  uint32_t fhIdxAt(int i) const {
    return static_cast<uint32_t>(st->dir.fhIdx[i]);
  }
  uint32_t fhTag8At(int i) const {
    return static_cast<uint32_t>(st->dir.fhTag8[i]);
  }
  uint32_t fhTag7At(int i) const {
    return static_cast<uint32_t>(st->dir.fhTag7[i]);
  }
  uint64_t ghr() const {
    return (static_cast<uint64_t>(static_cast<uint32_t>(st->dir.GHR_1)) << 32) |
           static_cast<uint32_t>(st->dir.GHR_2);
  }
};

// ---- training requests (decoded once at the top of work()) ----
struct TrainReq {
  bool valid = false;
  bool isJump = false;  // updateJump vs update
  bool taken = false;
  bool isCall = false;
  bool isRet = false;
  bool isIndirect = false;  // main-tree Cand never sets it on either port
  uint32_t pc = 0;
  uint32_t target = 0;
  uint64_t ghr = 0;
  TAGESCMeta meta{};
};

// ---- per-cycle write intents (fixed capacity, no per-cycle heap use) ----
enum TabKind : uint8_t {
  T_T0,
  T_LHT,
  T_TN_V,
  T_TN_TAG,
  T_TN_CTR,
  T_TN_U,
  T_UA,
  T_COND,
  T_BTB_APC,
  T_BTB_TGT,
  T_BTB_V,
  T_BTB_UN,
  T_BTB_CALL,
  T_BTB_RET,
  T_BTB_IND,
  T_BHT,
  T_TC,
  T_TCV,
};
struct TabEntry {
  uint8_t kind;
  uint8_t bank;  // Tn table index (else 0)
  uint16_t idx;
  uint32_t val;
};
struct Plan {
  TabEntry tab[64];
  uint32_t nTab = 0;
  bool ghr_we = false;  // BRU port only (speculative GHR); CDB keeps it false
  uint64_t ghr_val = 0;
  uint32_t lfsr_steps = 0;      // LFSR steps triggered by this port
  uint32_t bankTick_steps = 0;  // update events from this port (bankTickCtr)
  bool bht_we = false;
  uint16_t bht_idx = 0;
  uint32_t bht_val = 0;
  uint8_t bht_tabIdx = 0;  // tab slot of this port's T_BHT (RMW fixup)
  void put(uint8_t kind, uint32_t idx, uint32_t val, uint8_t bank = 0) {
    dark::debug::assert(nTab < 64, "plan overflow: raise Plan::tab");
    if (nTab >= 64) return;
    if (kind == T_BHT) {
      // Mirror for the per-port BHT collision check (each port puts one
      // T_BHT). `merged` may hold two, so no uniqueness assert here.
      bht_we = true;
      bht_idx = idx;
      bht_val = val;
      bht_tabIdx = static_cast<uint8_t>(nTab);  // slot this entry occupies
    }
    tab[nTab++] = {kind, bank, static_cast<uint16_t>(idx), val};
  }
};
// Per-port plan bounds (fi: BTB only, <=6; bru/cdb: <=18 each) and their
// merge (<=42) must fit Plan::tab[64], else put() drops entries.
static_assert(6 + 18 + 18 <= 64, "merged plan worst case must fit Plan::tab");

// ---- update: pure function, reads snap, fills a plan, zero `<=`.
//      BRU port may set ghr/ras; CDB port is commit (tables only). ----
Plan updatePlan(const Snap &snap, const TrainReq &req) {
  Plan p;
  p.bankTick_steps = 1;
  const uint32_t p2 = req.pc >> 2;
  // Fold req.ghr (the fetch-time snapshot), not the live registers: they run
  // ahead. The fold is the compile-time helper, so its chunk count is fixed at
  // ceil(H/W) by construction and cannot drift again.
  uint32_t idx[TAGE_NTABLES] = {};
  uint8_t tags[TAGE_NTABLES] = {};
  bool hit[TAGE_NTABLES] = {};
  for (int i = 0; i < TAGE_NTABLES; ++i) {
    idx[i] = (foldIdx(i, req.ghr) ^ (p2 & ((1u << TAGE_IDX_BIT) - 1))) &
             ((1u << TAGE_IDX_BIT) - 1);
    tags[i] = static_cast<uint8_t>(
        (foldTag8(i, req.ghr) ^ foldTag7(i, req.ghr) ^
         (p2 & ((1u << TAGE_TAG_BIT) - 1))) &
        ((1u << TAGE_TAG_BIT) - 1));
    hit[i] = snap.tnValid(i, idx[i]) && snap.tnTag(i, idx[i]) == tags[i];
  }

  int prov = req.meta.provValid ? static_cast<int>(req.meta.provIdx) : -1;
  p.put(T_COND, p2 & (CONDSEEN_CAP - 1), 1);
  const uint32_t lhtIdx = p2 & (LHT_CAP - 1);
  const uint32_t t0index = (p2 ^ snap.LHT(lhtIdx)) & (T0_CAP - 1);
  uint32_t t0v = snap.t0(t0index);
  if (req.taken) {
    if (t0v < 3) ++t0v;
  } else {
    if (t0v > 0) --t0v;
  }
  p.put(T_T0, t0index, t0v);
  p.put(T_LHT, lhtIdx, ((snap.LHT(lhtIdx) << 1) | (req.taken ? 1 : 0)) & 0xFFF);

  bool tageCorrect = (req.meta.tagePred == req.taken);
  if (prov >= 0 && hit[prov]) {
    uint32_t ctr = snap.tnCtr(prov, idx[prov]);
    if (req.taken) {
      if (ctr < 7) ++ctr;
    } else {
      if (ctr > 0) --ctr;
    }
    p.put(T_TN_CTR, idx[prov], ctr, static_cast<uint8_t>(prov));
    uint32_t u = snap.tnU(prov, idx[prov]);
    if (tageCorrect && req.meta.altPred != req.taken) {
      if (u < 3) ++u;
    } else if (!tageCorrect) {
      if (u > 0) --u;
    }
    p.put(T_TN_U, idx[prov], u, static_cast<uint8_t>(prov));
  }

  if (prov >= 0 && (req.meta.provCtr == 3 || req.meta.provCtr == 4)) {
    uint32_t ua = snap.useAltOnNa(p2 & 127);
    if (req.meta.altPred == req.taken && req.meta.tagePred != req.taken) {
      if (ua < 15) ++ua;
    } else if (req.meta.altPred != req.taken &&
               req.meta.tagePred == req.taken) {
      if (ua > 0) --ua;
    }
    p.put(T_UA, p2 & 127, ua);
  }

  const bool provConfident =
      prov >= 0 && (req.meta.provCtr <= 1 || req.meta.provCtr >= 6);
  if (!tageCorrect && !(req.meta.altPred == req.taken && provConfident)) {
    const int start = prov + 1;
    bool allocated = false;
    uint8_t lfsrVal =
        static_cast<uint8_t>(static_cast<uint32_t>(snap.st->dir.lfsr));
    lfsrVal = static_cast<uint8_t>((lfsrVal & 1) ? ((lfsrVal >> 1) ^ LFSR_TAPS)
                                                 : (lfsrVal >> 1));
    if (lfsrVal == 0) lfsrVal = LFSR_SEED;
    p.lfsr_steps = 1;
    uint8_t lfsrPick = lfsrVal;
    for (int k = 0; k < TAGE_NTABLES; ++k) {
      if (allocated) continue;
      // &(N-1), NOT modulo: TAGE_NTABLES is a power of two, so `%` would
      // infer a real divider here (guarded by the static_assert above).
      int i = start + ((lfsrPick >> (k << 1)) & (TAGE_NTABLES - 1));
      if (i < 0) i = 0;
      if (i >= TAGE_NTABLES) continue;
      if (!hit[i] || snap.tnU(i, idx[i]) == 0) {
        uint32_t ctrv = req.taken ? 4 : 3;
        p.put(T_TN_V, idx[i], 1, static_cast<uint8_t>(i));
        p.put(T_TN_TAG, idx[i], tags[i], static_cast<uint8_t>(i));
        p.put(T_TN_CTR, idx[i], ctrv, static_cast<uint8_t>(i));
        p.put(T_TN_U, idx[i], 0, static_cast<uint8_t>(i));
        allocated = true;
      }
    }
    if (!allocated) {
      for (int i = 0; i < TAGE_NTABLES; ++i) {
        if (i >= start) {
          uint32_t u = snap.tnU(i, idx[i]);
          if (u > 0) --u;
          p.put(T_TN_U, idx[i], u, static_cast<uint8_t>(i));
        }
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
  uint32_t bhr = snap.BHT(p2 & (BHT_CAP - 1));
  p.put(T_BHT, p2 & (BHT_CAP - 1), ((bhr << 1) | (req.taken ? 1 : 0)) & 0xFF);
  return p;
}

Plan updateJumpPlan(const Snap &snap, const TrainReq &req) {
  Plan p;
  p.bankTick_steps = 0;  // jumps do not advance bankTickCtr (main-tree)
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

  const uint32_t bhr = snap.BHT(p2 & (BHT_CAP - 1));
  // main tree gates Target-Cache training on isIndirect too (never set on the
  // CDB candidate -> never trained); verbatim equivalence.
  if (req.isIndirect && req.isCall == false &&
      req.isRet == false) {  // true indirect
    const uint32_t tcHash = (p2 ^ bhr) & (TARGETCACHE_CAP - 1);
    p.put(T_TC, tcHash, req.target);
    p.put(T_TCV, tcHash, 1);
  }
  p.put(T_BHT, p2 & (BHT_CAP - 1), ((bhr << 1) | 1) & 0xFF);
  return p;
}
}  // namespace

PredictInfo BPU::predict(int32_t pc) const {
  Snap snap(this);
  const uint32_t p2 = static_cast<uint32_t>(pc) >> 2;
  const uint32_t lhtIdx = p2 & (LHT_CAP - 1);
  const uint32_t t0index = (p2 ^ snap.LHT(lhtIdx)) & (T0_CAP - 1);
  const bool basePred = snap.t0(t0index) >= 2;
  bool hit[TAGE_NTABLES] = {};
  uint32_t idx[TAGE_NTABLES] = {};
  uint8_t tags[TAGE_NTABLES] = {};
  for (int i = 0; i < TAGE_NTABLES; ++i) {
    idx[i] = (snap.fhIdxAt(i) ^ (p2 & ((1u << TAGE_IDX_BIT) - 1))) &
             ((1u << TAGE_IDX_BIT) - 1);
    tags[i] = static_cast<uint8_t>((snap.fhTag8At(i) ^ snap.fhTag7At(i) ^
                                    (p2 & ((1u << TAGE_TAG_BIT) - 1))) &
                                   ((1u << TAGE_TAG_BIT) - 1));
    hit[i] = snap.tnValid(i, idx[i]) && snap.tnTag(i, idx[i]) == tags[i];
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
    altPred = snap.tnCtr(alt, idx[alt]) >= 4;
  else if (prov >= 0)
    altPred = basePred;
  bool tagePred = basePred;
  uint8_t provCtr = 0, provU = 0;
  bool provValid = false;
  if (prov >= 0) {
    provValid = true;
    provCtr = static_cast<uint8_t>(snap.tnCtr(prov, idx[prov]));
    provU = static_cast<uint8_t>(snap.tnU(prov, idx[prov]));
    const bool weak = (provCtr == 3 || provCtr == 4);
    const bool useAlt = snap.useAltOnNa(p2 & 127) >= 8;
    tagePred = (weak && useAlt) ? altPred : (provCtr >= 4);
  }
  bool taken = tagePred;
  const auto BTB_index = p2 & (BTB_CAP - 1);
  bool btbHit = snap.BTBValid(BTB_index) &&
                snap.BTBActualPC(BTB_index) == static_cast<uint32_t>(pc);
  if (btbHit && snap.BTBUncond(BTB_index)) taken = true;
  const uint32_t bhr = snap.BHT(p2 & (BHT_CAP - 1));
  const uint32_t tcHash = (p2 ^ bhr) & (TARGETCACHE_CAP - 1);
  const bool tcUsable = btbHit && snap.BTBIndirect(BTB_index) &&
                        !snap.BTBCall(BTB_index) && !snap.BTBRet(BTB_index) &&
                        snap.TargetValid(tcHash);
  // RET with empty RAS: don't use BTB target 0, treat as not taken (wild fetch
  // fix)
  bool isRet = snap.BTBRet(BTB_index);
  bool rasEmpty = static_cast<uint32_t>(tgt.RAS_top) == 0;
  if (isRet && rasEmpty) {
    btbHit = false;
    taken = false;
  }
  int32_t predictPC = pc + 4;
  if (taken && btbHit) {
    if (isRet && static_cast<uint32_t>(tgt.RAS_top) > 0)
      predictPC = static_cast<int32_t>(static_cast<uint32_t>(
          tgt.RAS[(static_cast<uint32_t>(tgt.RAS_top) - 1) & (RAS_CAP - 1)]
              .retPC));
    else if (tcUsable)
      predictPC = static_cast<int32_t>(snap.TargetCache(tcHash));
    else
      predictPC = static_cast<int32_t>(snap.BTBTarget(BTB_index));
  }
  PredictInfo out{taken, predictPC};
  out.btbHit = btbHit;
  out.unconditional = btbHit && snap.BTBUncond(BTB_index);
  out.meta.provValid = provValid;
  out.meta.provIdx = provValid ? static_cast<uint8_t>(prov) : 0;
  out.meta.provCtr = provCtr;
  out.meta.provU = provU;
  out.meta.altPred = altPred;
  out.meta.tagePred = tagePred;
  out.meta.baseCnt = static_cast<uint8_t>(snap.t0(t0index));
  out.condSeen = snap.condSeen(p2 & (CONDSEEN_CAP - 1));
  return out;
}

// Fetch-direction guard (retired FetchDecision::build); the squash source is
// flushArbiter.needSquash, as the former arbitResult() bridge read.
bool BPU::fetchAllowed() const {
  return !static_cast<bool>(fetchCtx.squashNeed) &&
         !static_cast<bool>(fetchCtx.haltFetched) &&
         !static_cast<bool>(fetchCtx.fqFull) &&
         !static_cast<bool>(fetchCtx.imemReqFull);
}

// ---- fetch-stage prediction bundle (retired FetchDecision::build).
//      Each mid net calls predict() once; re-runs stay bit-identical. ----
void BPU::wire_output() {
  mid.predPC = [this]() -> uint32_t {
    if (!fetchAllowed()) return 0u;
    const PredictInfo p =
        predict(static_cast<int32_t>(static_cast<uint32_t>(fetchCtx.pc)));
    return p.taken ? static_cast<uint32_t>(p.predictPC)
                   : static_cast<uint32_t>(fetchCtx.pc) + 4u;
  };
  mid.packed = [this]() -> uint32_t {
    if (!fetchAllowed()) return 0u;
    const PredictInfo p =
        predict(static_cast<int32_t>(static_cast<uint32_t>(fetchCtx.pc)));
    // shift/shiftValue branch structure verbatim from build():
    // btbHit wins over condSeen; unconditional forces shiftValue.
    const bool shift = p.btbHit || p.condSeen;
    const bool shiftValue = p.btbHit ? (p.unconditional ? true : p.taken)
                                     : (p.condSeen ? p.taken : false);
    // ckptId occupies packed bits [7:2] (shift/shiftValue own bits 0/1),
    // so it must be shifted into place before the flag bits are ORed in.
    uint32_t v = (static_cast<uint32_t>(getNextCkptId()) & 0x3Fu) << 2;
    if (shift) v |= 1u << 0;
    if (shiftValue) v |= 1u << 1;
    if (p.meta.provValid) v |= 1u << 8;
    v |= (static_cast<uint32_t>(p.meta.provIdx) & 0x3u) << 9;
    v |= (static_cast<uint32_t>(p.meta.provCtr) & 0x7u) << 11;
    v |= (static_cast<uint32_t>(p.meta.provU) & 0x3u) << 14;
    if (p.meta.altPred) v |= 1u << 16;
    if (p.meta.tagePred) v |= 1u << 17;
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
  BPUSnapshot ckptSnap;
  ckptSnap.GHR_snapshot = getGHR();
  ckptSnap.alignHead = static_cast<uint32_t>(tgt.alignHead);
  ckptSnap.alignTail = static_cast<uint32_t>(tgt.alignTail);
  ckptSnap.RAS_top = static_cast<uint32_t>(tgt.RAS_top);
  return ckptSnap;
}

void BPU::work() {
  // Cycle-0 boot: t0/useAltOnNa need non-zero init. Runs in parallel with the
  // normal logic -- cycle 0 has ROB/BRU empty, so no training can race it.
  bool boot = !static_cast<bool>(bootDone);
  if (boot) {
    for (int i = 0; i < T0_CAP; ++i) dir.t0[i] <= 1;
    for (int i = 0; i < 128; ++i) dir.useAltOnNa[i] <= 8;
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
    bool correct =
        pcResult == static_cast<uint32_t>(rob.robPredictPC[brRobTag & 0x3F]);
    if (correct) ++branchCorrect;
    if (!needSquash || ROB::isOlder(brRobTag, squashTag)) {
      trBru.valid = true;
      trBru.isJump = false;
      trBru.pc = pcFrom;
      trBru.taken = pcResult != pcFrom + 4;
      trBru.target = pcResult;
      auto cid = static_cast<uint32_t>(rob.robCkptId[brRobTag & 0x3F]);
      // bpCkpt/tmeta are written only by fetch allocation and read by squash;
      // same-cycle alloc and rollback use distinct ckptIds -- no race.
      trBru.ghr =
          (static_cast<uint64_t>(static_cast<uint32_t>(bpCkpt[cid].GHR_1))
           << 32) |
          static_cast<uint32_t>(bpCkpt[cid].GHR_2);
      trBru.meta.provValid = static_cast<bool>(dir.tmeta[cid].provValid);
      trBru.meta.provIdx =
          static_cast<uint8_t>(static_cast<uint32_t>(dir.tmeta[cid].provIdx));
      trBru.meta.provCtr =
          static_cast<uint8_t>(static_cast<uint32_t>(dir.tmeta[cid].provCtr));
      trBru.meta.provU =
          static_cast<uint8_t>(static_cast<uint32_t>(dir.tmeta[cid].provU));
      trBru.meta.altPred = static_cast<bool>(dir.tmeta[cid].altPred);
      trBru.meta.tagePred = static_cast<bool>(dir.tmeta[cid].tagePred);
      trBru.meta.baseCnt =
          static_cast<uint8_t>(static_cast<uint32_t>(dir.tmeta[cid].baseCnt));
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
      if (correct) ++branchCorrect;
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
    p_bru =
        trBru.isJump ? updateJumpPlan(snap, trBru) : updatePlan(snap, trBru);
  if (trCdb.valid) p_cdb = updateJumpPlan(snap, trCdb);
  // CDB port is commit: ghr_we stays false (neither plan sets it)

  // ---- fetch allocation (BRU-port speculative: bpCkpt/tmeta/GHR
  // shift/nextCkptId) ----
  uint64_t ghrLocal = snap.ghr();
  uint32_t nextCkpt = static_cast<uint32_t>(nextCkptId);
  if (static_cast<bool>(fetchOut.valid)) {
    auto ckid = static_cast<uint32_t>(fetchOut.ckptId);
    BPUSnapshot ckptSnap = snapshotCheckPoint();
    bpCkpt[ckid].GHR_1 <= static_cast<uint32_t>(ckptSnap.GHR_snapshot >> 32);
    bpCkpt[ckid].GHR_2 <= static_cast<uint32_t>(ckptSnap.GHR_snapshot);
    bpCkpt[ckid].alignHead <= static_cast<uint32_t>(ckptSnap.alignHead);
    bpCkpt[ckid].alignTail <= static_cast<uint32_t>(ckptSnap.alignTail);
    bpCkpt[ckid].RAS_top <= static_cast<uint32_t>(ckptSnap.RAS_top);
    dir.tmeta[ckid].provValid <= static_cast<bool>(fetchOut.provValid);
    dir.tmeta[ckid].provIdx <= static_cast<uint32_t>(fetchOut.provIdx);
    dir.tmeta[ckid].provCtr <= static_cast<uint32_t>(fetchOut.provCtr);
    dir.tmeta[ckid].provU <= static_cast<uint32_t>(fetchOut.provU);
    dir.tmeta[ckid].altPred <= static_cast<bool>(fetchOut.altPred);
    dir.tmeta[ckid].tagePred <= static_cast<bool>(fetchOut.tagePred);
    dir.tmeta[ckid].baseCnt <= static_cast<uint32_t>(fetchOut.baseCnt);
    if (static_cast<bool>(fetchOut.shift))
      ghrLocal =
          (ghrLocal << 1) | (static_cast<bool>(fetchOut.shiftValue) ? 1u : 0u);
    nextCkpt = (ckid + 1) & (CKPT_CAP - 1);
  }

  // ---- RAS / alignQueue local mirror (BRU-port fetchInfo + squash rewind
  // override) ----
  uint32_t rasTop = static_cast<uint32_t>(tgt.RAS_top);
  uint32_t alignTail = static_cast<uint32_t>(tgt.alignTail);
  uint32_t alignHead = static_cast<uint32_t>(tgt.alignHead);
  // Pre-fi-block base (main-tree semantics): the squash rewind range comes
  // from the comb snapshot, so this tick's journal entries survive.
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
    // Early training (BRU-port fetch side; same-slot conflicts arbitrated in
    // commit, fetch highest)
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

  // Checkpoint allocation and squash rollback never coincide: the allocated
  // ckptId is always newer (ring distance >= 1), so bpCkpt reads never race.
  dark::debug::assert(!(static_cast<bool>(fetchOut.valid) && needSquash),
                      "fetch-alloc and squash-rollback in the same cycle");
  // ---- squash restore (highest priority over all speculative state) ----
  if (needSquash) {
    uint32_t ckid = squashCkpt & (CKPT_CAP - 1);
    uint64_t ckptGHR =
        (static_cast<uint64_t>(static_cast<uint32_t>(bpCkpt[ckid].GHR_1))
         << 32) |
        static_cast<uint32_t>(bpCkpt[ckid].GHR_2);
    uint32_t ckptAlignTail = static_cast<uint32_t>(bpCkpt[ckid].alignTail);
    uint32_t ckptAlignHead = static_cast<uint32_t>(bpCkpt[ckid].alignHead);
    uint32_t ckptRasTop = static_cast<uint32_t>(bpCkpt[ckid].RAS_top);
    uint32_t curTail = alignTailPreFi;
    uint32_t base = ckptAlignTail;
    // mod-256 ring distance (alignTail is 8-bit): the main tree computes this
    // in uint8_t, so a uint32 subtraction would underflow and replay garbage.
    uint32_t dist = (curTail - base) & 0xFF;
    for (int k = 0; k < ALIGNQ_CAP; ++k) {
      if (static_cast<uint32_t>(k) >= dist) continue;
      uint32_t pos = curTail - 1 - static_cast<uint32_t>(k);
      uint32_t idx = alIndex[pos & (ALIGNQ_CAP - 1)] & (RAS_CAP - 1);
      rasRetPC[idx] = alAddr[pos & (ALIGNQ_CAP - 1)];
      rasTimes[idx] = alTimes[pos & (ALIGNQ_CAP - 1)];
    }
    ghrLocal = ckptGHR;
    alignTail = ckptAlignTail;
    alignHead = ckptAlignHead;
    rasTop = ckptRasTop;
    nextCkpt = (ckid + 1) & (CKPT_CAP - 1);
  }

  // ---- commit: resource-typed arbitration, explicit next-state mux ----
  {
    // BHT dual-port collision: bru then cdb -> ((bruVal << 1) | 1). Rewrite
    // the cdb tab entry (mergeIn/apply read tab[], not the bht_val mirror).
    if (p_bru.bht_we && p_cdb.bht_we && p_bru.bht_idx == p_cdb.bht_idx)
      p_cdb.tab[p_cdb.bht_tabIdx].val = ((p_bru.bht_val << 1) | 1u) & 0xFFu;
    // bankTickCtr accumulation (only update advances it; updateJump does not)
    uint32_t btc = static_cast<uint32_t>(dir.bankTickCtr);
    btc += p_bru.bankTick_steps + p_cdb.bankTick_steps;
    bool tick = false;
    if (btc >= BANKTICK_MAX) {
      tick = true;
      btc -= BANKTICK_MAX;
    }

    // Merge fi > cdb > bru (first-in wins) so every physical Register is
    // assigned at most once per cycle.
    Plan merged;
    auto mergeIn = [&](const Plan &src) {
      for (uint32_t q = 0; q < src.nTab; ++q) {
        const auto &e = src.tab[q];
        bool dup = false;
        for (uint32_t m = 0; m < merged.nTab; ++m)
          if (merged.tab[m].kind == e.kind && merged.tab[m].bank == e.bank &&
              merged.tab[m].idx == e.idx)
            dup = true;
        if (!dup) merged.put(e.kind, e.idx, e.val, e.bank);
      }
    };
    mergeIn(p_fi);
    mergeIn(p_cdb);
    mergeIn(p_bru);

    // tn.u next-state mux, priority tick < bru < cdb. Tick cycles scan the
    // whole table (u >>= 1); non-tick cycles write only plan-touched slots.
    auto tnUVal = [&](const Plan &src, int t, uint32_t j,
                      bool &hit) -> uint32_t {
      for (uint32_t q = 0; q < src.nTab; ++q)
        if (src.tab[q].kind == T_TN_U && src.tab[q].bank == t &&
            src.tab[q].idx == j) {
          hit = true;
          return src.tab[q].val;
        }
      hit = false;
      return 0;
    };
    if (tick) {
      for (int t = 0; t < TAGE_NTABLES; ++t) {
        for (int j = 0; j < (1 << TAGE_IDX_BIT); ++j) {
          uint32_t nextU =
              static_cast<uint32_t>(dir.tn[t][j].u) >> 1;  // tick base
          bool hitBru, hitCdb;
          uint32_t valBru = tnUVal(p_bru, t, j, hitBru);
          uint32_t valCdb = tnUVal(p_cdb, t, j, hitCdb);
          if (hitBru) nextU = valBru;  // bru din overrides tick decay
          if (hitCdb) nextU = valCdb;  // cdb din highest among bru/tick
          dir.tn[t][j].u <= nextU;
        }
      }
    } else {
      // non-tick: only plan-touched u slots
      for (uint32_t q = 0; q < merged.nTab; ++q) {
        const auto &e = merged.tab[q];
        if (e.kind == T_TN_U) dir.tn[e.bank][e.idx].u <= e.val;
      }
    }

    // Other tables: single write per plan entry (merged already deduplicated).
    auto apply = [&](const Plan &src) {
      for (uint32_t k = 0; k < src.nTab; ++k) {
        const auto &e = src.tab[k];
        switch (e.kind) {
          case T_T0:
            dir.t0[e.idx] <= e.val;
            break;
          case T_LHT:
            dir.LHT[e.idx] <= e.val;
            break;
          case T_TN_V:
            dir.tn[e.bank][e.idx].valid <= e.val;
            break;
          case T_TN_TAG:
            dir.tn[e.bank][e.idx].tag <= e.val;
            break;
          case T_TN_CTR:
            dir.tn[e.bank][e.idx].ctr <= e.val;
            break;
          case T_UA:
            dir.useAltOnNa[e.idx] <= e.val;
            break;
          case T_COND:
            tgt.condSeen[e.idx] <= e.val;
            break;
          case T_BTB_APC:
            tgt.BTB[e.idx].actualPC <= e.val;
            break;
          case T_BTB_TGT:
            tgt.BTB[e.idx].target <= e.val;
            break;
          case T_BTB_V:
            tgt.BTB[e.idx].valid <= e.val;
            break;
          case T_BTB_UN:
            tgt.BTB[e.idx].unconditional <= e.val;
            break;
          case T_BTB_CALL:
            tgt.BTB[e.idx].isCall <= e.val;
            break;
          case T_BTB_RET:
            tgt.BTB[e.idx].isRet <= e.val;
            break;
          case T_BTB_IND:
            tgt.BTB[e.idx].isIndirect <= e.val;
            break;
          case T_BHT:
            tgt.BHT[e.idx] <= e.val;
            break;
          case T_TC:
            tgt.TargetCache[e.idx] <= e.val;
            break;
          case T_TCV:
            tgt.TargetValid[e.idx] <= e.val;
            break;
          default:
            break;
        }
      }
    };
    apply(merged);

    // Scalar event stepping. Enabled write only: an unconditional writeback
    // would read _M_old=0 and clobber the cycle-0 boot seed.
    dark::debug::assert(p_bru.lfsr_steps <= 1 && p_cdb.lfsr_steps <= 1,
                        "lfsr unroll depth 2 assumes <=1 step per port");
    uint32_t lfsrSteps = p_bru.lfsr_steps + p_cdb.lfsr_steps;
    if (lfsrSteps > 0) {
      uint32_t lfsr = static_cast<uint32_t>(dir.lfsr);
      if (lfsrSteps == 1) {
        lfsr = static_cast<uint8_t>((lfsr & 1) ? ((lfsr >> 1) ^ LFSR_TAPS)
                                               : (lfsr >> 1));
        if (lfsr == 0) lfsr = LFSR_SEED;
      } else {
        lfsr = static_cast<uint8_t>((lfsr & 1) ? ((lfsr >> 1) ^ LFSR_TAPS)
                                               : (lfsr >> 1));
        if (lfsr == 0) lfsr = LFSR_SEED;
        lfsr = static_cast<uint8_t>((lfsr & 1) ? ((lfsr >> 1) ^ LFSR_TAPS)
                                               : (lfsr >> 1));
        if (lfsr == 0) lfsr = LFSR_SEED;
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
  // ---- folded history: squash recompute / incremental step ----
  if (needSquash) {
    // Squash and fetch allocation are mutually exclusive (assert above), so
    // the two paths share if/else: each Register is written once per cycle.
    // Rollback restores only the GHR (the checkpointed quantity); the folds are
    // a derived view, so this rebuild is what resynchronises them after a
    // squash. Chunk counts come from ceil(H/W) inside refoldViewT.
    for (int i = 0; i < TAGE_NTABLES; ++i) {
      dir.fhIdx[i] <= foldIdx(i, ghrLocal);
      dir.fhTag8[i] <= static_cast<uint8_t>(foldTag8(i, ghrLocal));
      dir.fhTag7[i] <= static_cast<uint8_t>(foldTag7(i, ghrLocal));
    }
  } else if (static_cast<bool>(fetchOut.shift)) {
    const uint32_t b = static_cast<uint32_t>(fetchOut.shiftValue);
    const uint64_t og = snap.ghr();  // pre-shift GHR (discarded bit source)
    const uint32_t d5 = static_cast<uint32_t>(og >> 5) & 1u;    // H=6 bit
    const uint32_t d11 = static_cast<uint32_t>(og >> 11) & 1u;  // H=12
    const uint32_t d23 = static_cast<uint32_t>(og >> 23) & 1u;  // H=24
    const uint32_t d47 = static_cast<uint32_t>(og >> 47) & 1u;  // H=48
    const uint32_t disc[TAGE_NTABLES] = {d5, d11, d23, d47};

    // Incremental step: rotl1 (MSB wraps to LSB) ^ b ^ (disc << (H % W)).
    // Both the rotate width and the wrap amount come from compile-time
    // constants, so neither the W=7 rotate nor the H % W == 0 case (where the
    // wrap term is plain `disc`, NOT an absent term) can be hand-evaluated
    // wrong. Mask after the rotate: at W < 32 the `<< 1` can push the top bit
    // past position W-1 before `>> (W - 1)` folds it back.
    //
    // Each next value is built in a named local BEFORE the `<=`: Register<>'s
    // assignment operator returns void and `<=` binds tighter than `&`, so a
    // trailing `& M` written after the RHS would parse as `(reg <= x) & M`.
    for (int i = 0; i < TAGE_NTABLES; ++i) {
      constexpr uint32_t W = TAGE_IDX_BIT;
      constexpr uint32_t M = (1u << TAGE_IDX_BIT) - 1u;
      const uint32_t v = static_cast<uint32_t>(dir.fhIdx[i]);
      const uint32_t rot = ((v << 1) | (v >> (W - 1))) & M;
      dir.fhIdx[i] <= ((rot ^ b ^ (disc[i] << wrapShiftIdx[i])) & M);
    }
    for (int i = 0; i < TAGE_NTABLES; ++i) {
      constexpr uint32_t W = TAGE_TAG_BIT;
      constexpr uint32_t M = (1u << TAGE_TAG_BIT) - 1u;
      const uint32_t v = static_cast<uint32_t>(dir.fhTag8[i]);
      const uint32_t rot = ((v << 1) | (v >> (W - 1))) & M;
      dir.fhTag8[i] <= ((rot ^ b ^ (disc[i] << wrapShiftTag8[i])) & M);
    }
    for (int i = 0; i < TAGE_NTABLES; ++i) {
      constexpr uint32_t W = TAGE_TAG_BIT - 1;
      constexpr uint32_t M = (1u << (TAGE_TAG_BIT - 1)) - 1u;
      const uint32_t v = static_cast<uint32_t>(dir.fhTag7[i]);
      const uint32_t rot = ((v << 1) | (v >> (W - 1))) & M;
      dir.fhTag7[i] <= ((rot ^ b ^ (disc[i] << wrapShiftTag7[i])) & M);
    }
  }
  // no squash, no shift: no write, so folded history tracks the GHR

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
