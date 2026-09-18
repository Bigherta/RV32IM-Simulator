#include "../include/ICache.hpp"
#include <cassert>
#include <cstdint>

bool ICache::hit(uint32_t addr) const {
  auto index = (addr >> 4) & (CACHE_CAP - 1);
  return static_cast<bool>(blocks[index].valid) &&
         static_cast<uint32_t>(blocks[index].tag) == (addr >> 13);
}

void ICache::work() {
  // stage 3 flush: clear the speculative request queue (cache lines kept)
  if (needSquash) {
    for (int i = 0; i < REQUEST_CAP; ++i)
      requestBuffer[i].valid <= false;
    head <= 0;
  } else {
  const bool popE = static_cast<bool>(popConsume);
  const uint32_t oh = static_cast<uint32_t>(head);
  const uint32_t occ = occupancy();
  // post-pop view: the original tick read CPUstate.head/count AFTER its pop,
  // so the backfill gate and slot must use these locals, never the registers
  const uint32_t nh = popE ? ((oh + 1) & (REQUEST_CAP - 1)) : oh;
  const uint32_t nc = occ - (popE ? 1u : 0u);
  // stage 2 pop: self-release once FQ consumed the ICache head (write-own-only)
  if (popE) {
    requestBuffer[oh].valid <= false;
    requestBuffer[oh].ready <= false;
    head <= ((oh + 1) & (REQUEST_CAP - 1));
  }
  // stage 2 line refill: consume the IMEM line-return bus (word bundle,
  // direct assignment -- no packing on the cold path), fill the cache line
  // and backfill the placeholder entry
  if (static_cast<bool>(lineReturn.valid)) {
    const uint32_t la = static_cast<uint32_t>(lineReturn.lineAddr);
    auto cachelineIndex = (la >> 4) & (CACHE_CAP - 1);
    blocks[cachelineIndex].valid <= true;
    blocks[cachelineIndex].tag <= la >> 13;
    for (int k = 0; k < (CACHE_BLOCK_CAP >> 2); ++k)
      blocks[cachelineIndex].Data[k] <= static_cast<uint32_t>(lineReturn.data[k]);
    // backfill placeholder: only when the post-pop queue is non-empty and the
    // new head is still an occupied-but-not-ready entry. The slot already has
    // valid=1 from its push, so only raw_inst/ready flip here.
    if (nc > 0 && !static_cast<bool>(requestBuffer[nh].ready)) {
      uint32_t pc = static_cast<uint32_t>(requestBuffer[nh].PC);
      requestBuffer[nh].raw_inst <=
          static_cast<uint32_t>(lineReturn.data[(pc >> 2) & 3]);
      requestBuffer[nh].ready <= true;
    }
  }
  // stage 1 push new request: enqueue on a valid fetchDecision (hit -> ready,
  // miss -> placeholder); reads committed cache lines only (_M_old), so a
  // line refilled in this same cycle is correctly seen as a miss
  if (static_cast<bool>(fetchValid)) {
    const uint32_t pc = static_cast<uint32_t>(fetchPC);
    bool isHit = hit(pc);
    uint32_t raw_inst = 0;
    if (isHit)
      raw_inst = lineWord((pc >> 4) & (CACHE_CAP - 1), (pc >> 2) & 3);
    assert(occ < static_cast<uint32_t>(REQUEST_CAP));
    auto index = (oh + occ) & (REQUEST_CAP - 1);
    requestBuffer[index].raw_inst <= raw_inst;
    requestBuffer[index].PC <= pc;
    requestBuffer[index].predictPC <=
        static_cast<uint32_t>(fetchPredictPC);
    requestBuffer[index].ckptId <= static_cast<uint32_t>(fetchCkptId);
    requestBuffer[index].valid <= true;
    requestBuffer[index].ready <= isHit;
    // host-only profile counters (never read by the datapath)
    if (isHit)
      ++statHits;
    else
      ++statMisses;
  }
  }
}
