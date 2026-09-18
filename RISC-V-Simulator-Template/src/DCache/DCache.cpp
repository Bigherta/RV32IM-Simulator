#include "../include/DCache.hpp"
#include "../include/Memory.hpp"
#include "ROB.hpp"
#include "common.h"
#include <cassert>
#include <cstdint>

namespace {
// Phase encoding for DCacheInner.phase (Register<1>): READY=0, WAIT=1.
enum DCachePhase : uint32_t { DCACHE_READY = 0, DCACHE_WAIT = 1 };
} // namespace

void DCache::wire_output() {
  // ---- Output Wires read the committed _M_old registers (single point) ----
  busyOut = [this]() -> uint32_t { return static_cast<uint32_t>(busy); };
  // loadResp squash guard inlined: valid && (!needSquash || isOlder)
  loadRespValid = [this]() -> uint32_t {
    return (static_cast<bool>(lbValid) &&
            (!static_cast<bool>(squashNeed) ||
             ROB::isOlder(static_cast<uint32_t>(lbRobTag),
                          static_cast<uint32_t>(squashTag))))
               ? 1u
               : 0u;
  };
  loadRespMemIndex = [this]() -> uint32_t {
    return static_cast<uint32_t>(lbMemIndex);
  };
  loadRespRobTag = [this]() -> uint32_t {
    return static_cast<uint32_t>(lbRobTag);
  };
  loadRespValue = [this]() -> uint32_t {
    return static_cast<uint32_t>(lbValue);
  };
  reqReadValid = [this]() -> uint32_t {
    return static_cast<uint32_t>(rqReadValid);
  };
  reqReadAddr = [this]() -> uint32_t {
    return static_cast<uint32_t>(rqReadAddr);
  };
  reqWriteValid = [this]() -> uint32_t {
    return static_cast<uint32_t>(rqWriteValid);
  };
  reqWriteAddr = [this]() -> uint32_t {
    return static_cast<uint32_t>(rqWriteAddr);
  };
  for (int i = 0; i < DCACHE_BLOCK_CAP; ++i) {
    reqWriteLineData[i] = [this, i]() -> uint32_t {
      return static_cast<uint32_t>(rqWriteLineData[i]);
    };
  }
}

int DCache::decodeNBytes(uint32_t enc) {
  // 2b encoding on the decision bus: 0->1B, 1->2B, 2->4B
  return enc == 0 ? 1 : (enc == 1 ? 2 : 4);
}

int32_t DCache::extractValue(const uint8_t *datas, int off, int n,
                             bool isSigned) {
  // Sign-extended sub-word load:
  // a bare static_cast<int32_t> would leave the high bits zero for n<4 signed
  // reads.
  uint32_t rawData = 0;
  for (int i = 0; i < 4; ++i) {
    if (i < n)
      rawData |= static_cast<uint32_t>(datas[off + i]) << (i << 3);
  }
  if (isSigned && n == 1 && (rawData & 0x80u))
    rawData |= 0xFFFFFF00u;
  else if (isSigned && n == 2 && (rawData & 0x8000u))
    rawData |= 0xFFFF0000u;
  return static_cast<int32_t>(rawData);
}

DCache::Probe DCache::probe(uint32_t addr) const {
  // pure read over the plain line array: set/tag decode + way scan
  const uint32_t blockNum = addr >> 4;
  const uint32_t setIndex = blockNum & (NUM_OF_SETS - 1);
  const uint32_t tag = addr >> DCACHE_TAG_SHIFT;
  Probe p{false, 0};
  for (int i = 0; i < NUM_OF_WAYS; ++i) {
    if (cacheSets[setIndex].lines[i].valid &&
        cacheSets[setIndex].lines[i].tag == tag) {
      p.hit = true;
      p.way = static_cast<uint32_t>(i);
    }
  }
  return p;
}

uint32_t DCache::allocateWay(uint32_t set_index) const {
  // distribute a line without modifying anything: free way first, else
  // tree-PLRU victim. NB: (plru & 0x4) evaluates to 0 or 4, so the victim
  // choice must test truthiness -- the old "== 1" form was always false and
  // pinned every full-set eviction to way 0.
  int invalidIndex = -1;
  for (int i = 0; i < NUM_OF_WAYS; ++i) {
    if (cacheSets[set_index].lines[i].valid == 0 && invalidIndex == -1) {
      invalidIndex = i;
    }
  }
  uint32_t targetIndex;
  if (invalidIndex != -1) {
    targetIndex = static_cast<uint32_t>(invalidIndex);
  } else {
    const uint32_t plru = static_cast<uint32_t>(cacheSets[set_index].PLRU_bit);
    targetIndex = (plru & 0x4) ? ((plru & 0x1) ? 3u : 2u)
                               : ((plru & 0x2) ? 1u : 0u);
  }
#ifdef _DEBUG
  // PLRU is approximate LRU, so there is no strict-LRU assertion here; the
  // invalid path must always pick a free way. (NB: `!reg` is the framework's
  // bitwise NOT returning Bit<N> -- contextual bool needs static_cast.)
  if (invalidIndex != -1) {
    assert(!static_cast<bool>(cacheSets[set_index].lines[targetIndex].valid));
  }
#endif
  return targetIndex;
}

void DCache::work() {
  // ---- Phase READY: combinational accept of the dispatched decision ----
  if (static_cast<uint32_t>(phase) == DCACHE_READY) {
    const bool decValid = static_cast<bool>(decisionValid);
    const uint32_t decOp = static_cast<uint32_t>(decisionOp);
    const uint32_t addr = static_cast<uint32_t>(decisionAddr);
    const uint32_t blockNum = addr >> 4;
    const uint32_t setIndex = blockNum & (NUM_OF_SETS - 1);
    const int n = decodeNBytes(static_cast<uint32_t>(decisionNBytes));
    const Probe p = probe(addr);
    const bool opIsLoad = decOp == static_cast<uint32_t>(Operation::Load);
#ifdef _DEBUG
    const bool opIsStore = decOp == static_cast<uint32_t>(Operation::Store);
    // A request must never straddle a 16B line (mirrors main tree PrRd/PrWr).
    if (decValid && (opIsLoad || opIsStore))
      assert((addr & (DCACHE_BLOCK_CAP - 1)) + n <= DCACHE_BLOCK_CAP);
    // Accepting a new decision requires both DMEM ports to be drained: the
    // arbiter gates on !isBusy(), and busy covers the whole miss, so READY
    // implies the previous op fully completed (mirrors main tree DCache::tick).
    if (decValid)
      assert(!static_cast<bool>(dmemReadBusy) &&
             !static_cast<bool>(dmemWriteBusy));
#endif

    // ---- pulse single-write (every pulse register gets exactly one <= per
    // cycle; miss/state registers only on their event) ----
    // loadBuffer pulse: valid only on a clean hit load (1-cycle self answer)
    const bool lbHit = decValid && p.hit && opIsLoad;
    lbValid <= (lbHit ? 1u : 0u);
    lbValue <= (lbHit ? static_cast<uint32_t>(extractValue(
                            cacheSets[setIndex].lines[p.way].datas.data(),
                            addr & 0xF, n, static_cast<bool>(decisionIsSigned)))
                      : 0u);
    lbMemIndex <= (lbHit ? static_cast<uint32_t>(decisionMemIndex) : 0u);
    lbRobTag <= (lbHit ? static_cast<uint32_t>(decisionRobTag) : 0u);
    // forward request pulse: miss only (fill read + optional dirty writeback)
    const bool miss = decValid && !p.hit;
    const uint32_t targetWay = miss ? allocateWay(setIndex) : 0u;
    const bool dirtyVictim = miss && cacheSets[setIndex].lines[targetWay].dirty;
    rqReadValid <= (miss ? 1u : 0u);
    rqReadAddr <= (miss ? ((addr >> 4) << 4) : 0u);
    rqWriteValid <= (dirtyVictim ? 1u : 0u);
    if (dirtyVictim)
      ++statWritebacks;
    // Victim base address must be rebuilt from the VICTIM line's own tag,
    // not the incoming request's tag -- otherwise the dirty data lands on
    // the wrong frame and the refetch below reads back stale memory.
    rqWriteAddr <=
        (dirtyVictim
             ? static_cast<uint32_t>(((cacheSets[setIndex].lines[targetWay].tag
                                       << DCACHE_INDEX_BITS) +
                                      setIndex)
                                     << 4)
             : 0u);
    for (int i = 0; i < DCACHE_BLOCK_CAP; ++i) {
      rqWriteLineData[i] <=
          (dirtyVictim ? cacheSets[setIndex].lines[targetWay].datas[i] : 0u);
    }

    if (decValid) {
      if (p.hit) {
        ++statHits;
        // PLRU update: point every tree bit away from the hit way
        auto plru = static_cast<uint32_t>(cacheSets[setIndex].PLRU_bit);
        if (p.way < 2)
          plru |= 0x4;
        else
          plru &= ~0x4;
        if (p.way == 0)
          plru |= 0x2;
        else if (p.way == 1)
          plru &= ~0x2;
        else if (p.way == 2)
          plru |= 0x1;
        else
          plru &= ~0x1;
        cacheSets[setIndex].PLRU_bit <= plru;
        if (opIsLoad) {
          // clean-hit load: data already latched into the loadBuffer pulse
        } else {
          // store hit: merge into the line (write-back cache, no DMEM write)
          cacheSets[setIndex].lines[p.way].dirty <= true;
          for (int i = 0; i < 4; ++i) {
            if (i < n && addr + i < MEM_SIZE) {
              cacheSets[setIndex].lines[p.way].datas[(addr & 0xF) + i] =
                  (static_cast<uint32_t>(decisionValue) >> (i << 3)) & 0xFF;
            }
          }
        }
      } else {
        // miss: park the whole decision (identity included) + go WAIT.
        // PrRd/PrWr already pulsed the fill/ writeback request above.
        ++statMisses;
        parkOp <= decOp;
        parkValue <= static_cast<uint32_t>(decisionValue);
        parkAddr <= addr;
        parkIsSigned <= static_cast<uint32_t>(decisionIsSigned);
        parkNBytes <= static_cast<uint32_t>(decisionNBytes);
        parkRobTag <= static_cast<uint32_t>(decisionRobTag);
        parkMemIndex <= static_cast<uint32_t>(decisionMemIndex);
        parkTargetWay <= targetWay;
        busy <= 1u;
        phase <= DCACHE_WAIT;
      }
    }
  } else {
    // ---- Phase WAIT: fill + serve the parked op ----
    // forward request cleared every cycle (DMEM claims on the pulse only)
    rqReadValid <= 0u;
    rqReadAddr <= 0u;
    rqWriteValid <= 0u;
    rqWriteAddr <= 0u;
    for (int i = 0; i < DCACHE_BLOCK_CAP; ++i) {
      rqWriteLineData[i] <= 0u;
    }
    // DMEM completed at tick N (write live) -> comb N+1 snapshot -> visible
    // here via the dmem* Wires (read _M_old).
    const bool done =
        static_cast<bool>(dmemReplyReady) && !static_cast<bool>(dmemWriteBusy);
    const bool serveLoad = done && static_cast<uint32_t>(parkOp) ==
                                       static_cast<uint32_t>(Operation::Load);
    const bool serveStore = done && static_cast<uint32_t>(parkOp) ==
                                        static_cast<uint32_t>(Operation::Store);
    uint32_t lbValueNext = 0;
    if (done) {
      // fill the line from the completed read, then serve the parked op
      const uint32_t paddr = static_cast<uint32_t>(parkAddr);
      const uint32_t pset = (paddr >> 4) & (NUM_OF_SETS - 1);
      const uint32_t ptag = paddr >> DCACHE_TAG_SHIFT;
      const uint32_t pway = static_cast<uint32_t>(parkTargetWay);
      auto &line = cacheSets[pset].lines[pway];
      for (int i = 0; i < DCACHE_BLOCK_CAP; ++i) {
        line.datas[i] =
            static_cast<uint8_t>(static_cast<uint32_t>(dmemLineData[i]));
      }
      line.valid <= true;
      // dirty single write: fill clears it, a parked store re-sets it in the
      // same cycle -- two <= on one register would trip register.h:38 (_DEBUG)
      line.dirty <= serveStore;
      line.tag <= ptag;
      // PLRU update: point every tree bit away from the filled way
      auto plru = static_cast<uint32_t>(cacheSets[pset].PLRU_bit);
      if (pway < 2)
        plru |= 0x4;
      else
        plru &= ~0x4;
      if (pway == 0)
        plru |= 0x2;
      else if (pway == 1)
        plru &= ~0x2;
      else if (pway == 2)
        plru |= 0x1;
      else
        plru &= ~0x1;
      cacheSets[pset].PLRU_bit <= plru;
      if (serveLoad) {
        // datas were just filled from the DMEM reply, so reading the line is
        // equivalent to reading the reply bus; sign-extension lives only in
        // extractValue (single copy of the mask logic)
        lbValueNext = static_cast<uint32_t>(extractValue(
            line.datas.data(), static_cast<int>(paddr & 0xF),
            decodeNBytes(static_cast<uint32_t>(parkNBytes)),
            static_cast<bool>(parkIsSigned)));
      }
      if (serveStore) {
        const int pn = decodeNBytes(static_cast<uint32_t>(parkNBytes));
        for (int i = 0; i < 4; ++i) {
          if (i < pn && paddr + i < MEM_SIZE) {
            line.datas[(paddr & 0xF) + i] =
                (static_cast<uint32_t>(parkValue) >> (i << 3)) & 0xFF;
          }
        }
      }
      // retire the parked op, back to READY
      busy <= 0u;
      phase <= DCACHE_READY;
    }
    // loadBuffer pulse single-write
    lbValid <= (serveLoad ? 1u : 0u);
    lbValue <= lbValueNext;
    lbMemIndex <= (serveLoad ? static_cast<uint32_t>(parkMemIndex) : 0u);
    lbRobTag <= (serveLoad ? static_cast<uint32_t>(parkRobTag) : 0u);
  }
}
