#include "../include/DCache.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

void DCache::snapshotFrom(const DCache &other) {
  // Only pipeline state is copied; the line array belongs to the active
  // instance. comb() samples the selected old set separately for tick().
  busy = other.busy;
  phase = other.phase;
  request = other.request;
  loadBuffer = other.loadBuffer;
  cacheRequestBuffer = other.cacheRequestBuffer;
  hitCount = other.hitCount;
  missCount = other.missCount;
  flushSet = other.flushSet;
  flushWay = other.flushWay;
  flushDone = other.flushDone;
  flushWriteStarted = other.flushWriteStarted;
}

DCacheSetSnapshot DCache::sampleSet(const MemDispatchDecision &decision) const {
  DCacheSetSnapshot snapshot{};
  if (phase == Phase::FLUSH_SCAN || phase == Phase::FLUSH_WAIT) {
    snapshot.setIndex = flushSet;
    snapshot.way = flushWay;
    snapshot.oldSet = cacheSets[flushSet];
    return snapshot;
  }
  if (phase == Phase::READY && !decision.valid) return snapshot;
  const auto addr = phase == Phase::READY ? decision.request.address : cacheRequestBuffer.request.address;
  snapshot.setIndex = (addr >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
  snapshot.oldSet = cacheSets[snapshot.setIndex];
  for (int i = 0; i < NUM_OF_DCACHE_WAYS; ++i) {
    const auto &line = snapshot.oldSet.lines[i];
    if (line.valid && line.tag == (addr >> DCACHE_TAG_SHIFT)) {
      snapshot.hit = true;
      snapshot.way = static_cast<uint8_t>(i);
    }
  }
  if (!snapshot.hit) snapshot.way = AllocateLine(snapshot.setIndex);
  return snapshot;
}

uint8_t DCache::AllocateLine(int set_idx) const {
  if constexpr (NUM_OF_DCACHE_WAYS == 1) {
    return 0;
  } else {
    int invalidIndex = -1;
    for (int i = 0; i < (int)cacheSets[set_idx].lines.size(); i++) {
      if (!cacheSets[set_idx].lines[i].valid && invalidIndex == -1) {
        invalidIndex = i;
      }
    }
    if (invalidIndex != -1) return invalidIndex;
    // tree-PLRU victim: 0=left,1=right
    uint8_t plru = cacheSets[set_idx].plru;
    int victim = (plru & 0x4) ? ((plru & 0x1) ? 3 : 2) : ((plru & 0x2) ? 1 : 0);
    return victim;
  }
}

void DCache::tick(const DCacheInput &input, systemState &CPUstate) {
  DMEMRequest requestNext{};
  LoadResponse responseNext{};
  DCachePark parkNext = cacheRequestBuffer;
  bool busyNext = busy;
  auto phaseNext = phase;
  uint64_t hitsNext = hitCount, missesNext = missCount;
  auto flushSetNext = flushSet;
  auto flushWayNext = flushWay;
  auto flushDoneNext = flushDone;
  auto flushStartedNext = flushWriteStarted;
  const auto &snapshot = input.setSnapshot;
  auto extract = [](const uint8_t *bytes, const MemRequest &req) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
      if (i < req.nBytes) value |= uint32_t(bytes[(req.address & DCACHE_OFFSET_MASK) + i]) << (i << 3);
    if (req.isSigned && req.nBytes == 1 && (value & 0x80u)) value |= 0xFFFFFF00u;
    if (req.isSigned && req.nBytes == 2 && (value & 0x8000u)) value |= 0xFFFF0000u;
    return static_cast<int32_t>(value);
  };
  auto plruAfter = [](uint8_t old, uint8_t way) {
    uint8_t result = old;
    if (way < 2) result |= 4; else result &= ~4;
    if (way == 0) result |= 2; else if (way == 1) result &= ~2;
    else if (way == 2) result |= 1; else result &= ~1;
    return result;
  };
  if (phase == Phase::READY && input.flushRequested && !flushDone) {
    phaseNext = Phase::FLUSH_SCAN;
    busyNext = true;
    flushSetNext = 0;
    flushWayNext = 0;
  } else if (phase == Phase::FLUSH_SCAN || phase == Phase::FLUSH_WAIT) {
    bool advance = false;
    if (phase == Phase::FLUSH_SCAN) {
      const auto &line = snapshot.oldSet.lines[flushWay];
      if (line.valid && line.dirty) {
        requestNext.writeValid = true;
        requestNext.write.address = ((line.tag << DCACHE_INDEX_BITS) | flushSet) << DCACHE_OFFSET_BITS;
        requestNext.write.remainCycle = MEM_LATENCY;
        std::memcpy(requestNext.write.lineData, line.datas, DCACHE_BLOCK_CAP);
        phaseNext = Phase::FLUSH_WAIT;
        flushStartedNext = false;
      } else advance = true;
    } else {
      if (input.DMEMModule.isWriteBusy()) flushStartedNext = true;
      if (flushWriteStarted && !input.DMEMModule.isWriteBusy()) {
        CPUstate.DCacheModule.cacheSets[flushSet].lines[flushWay].dirty = false;
        advance = true;
      }
    }
    if (advance) {
      if (flushWay == NUM_OF_DCACHE_WAYS - 1 && flushSet == NUM_OF_DCACHE_SETS - 1) {
        flushDoneNext = true;
        phaseNext = Phase::READY;
        busyNext = false;
      } else {
        if (flushWay == NUM_OF_DCACHE_WAYS - 1) { flushWayNext = 0; flushSetNext = flushSet + 1; }
        else flushWayNext = flushWay + 1;
        phaseNext = Phase::FLUSH_SCAN;
      }
    }
  } else if (phase == Phase::READY && !input.flushRequested && input.decision.valid) {
    assert(!input.DMEMModule.isReadBusy() && !input.DMEMModule.isWriteBusy());
    const auto &req = input.decision.request;
    assert((req.address & DCACHE_OFFSET_MASK) + req.nBytes <= DCACHE_BLOCK_CAP);
    const auto &oldLine = snapshot.oldSet.lines[snapshot.way];
    if (snapshot.hit) {
      ++hitsNext;
      if (req.op == Operation::Load) {
        responseNext = {true, req.memIndex, req.robTag, extract(oldLine.datas, req)};
      } else {
        CPUstate.DCacheModule.cacheSets[snapshot.setIndex].lines[snapshot.way].dirty = true;
        for (int i = 0; i < 4; ++i)
          if (i < req.nBytes && req.address + i < MEM_SIZE)
            CPUstate.DCacheModule.cacheSets[snapshot.setIndex].lines[snapshot.way].datas[(req.address & DCACHE_OFFSET_MASK) + i] = (uint32_t(req.value) >> (i << 3)) & 255u;
      }
      if constexpr (NUM_OF_DCACHE_WAYS > 1)
        CPUstate.DCacheModule.cacheSets[snapshot.setIndex].plru = plruAfter(snapshot.oldSet.plru, snapshot.way);
    } else {
      ++missesNext;
      parkNext = {req, true, snapshot.way};
      busyNext = true;
      phaseNext = Phase::WAIT;
      requestNext.readValid = true;
      requestNext.read.address = req.address & ~DCACHE_OFFSET_MASK;
      requestNext.read.remainCycle = MEM_LATENCY;
      if (oldLine.dirty) {
        requestNext.writeValid = true;
        requestNext.write.address = ((oldLine.tag << DCACHE_INDEX_BITS) | snapshot.setIndex) << DCACHE_OFFSET_BITS;
        requestNext.write.remainCycle = MEM_LATENCY;
        std::memcpy(requestNext.write.lineData, oldLine.datas, DCACHE_BLOCK_CAP);
      }
    }
  } else if (phase == Phase::WAIT && input.DMEMModule.isReplyReady() && !input.DMEMModule.isWriteBusy()) {
    const auto &req = cacheRequestBuffer.request;
    const auto way = cacheRequestBuffer.targetWay;
    Cacheline lineNext{};
    lineNext.valid = true;
    lineNext.tag = req.address >> DCACHE_TAG_SHIFT;
    lineNext.dirty = req.op == Operation::Store;
    std::memcpy(lineNext.datas, input.DMEMModule.reply().lineData, DCACHE_BLOCK_CAP);
    if (lineNext.dirty) {
      for (int i = 0; i < 4; ++i)
        if (i < req.nBytes && req.address + i < MEM_SIZE)
          lineNext.datas[(req.address & DCACHE_OFFSET_MASK) + i] = (uint32_t(req.value) >> (i << 3)) & 255u;
    } else responseNext = {true, req.memIndex, req.robTag, extract(lineNext.datas, req)};
    CPUstate.DCacheModule.cacheSets[snapshot.setIndex].lines[way] = lineNext;
    if constexpr (NUM_OF_DCACHE_WAYS > 1)
      CPUstate.DCacheModule.cacheSets[snapshot.setIndex].plru = plruAfter(snapshot.oldSet.plru, way);
    parkNext = {};
    busyNext = false;
    phaseNext = Phase::READY;
  }
  CPUstate.DCacheModule.request = requestNext;
  CPUstate.DCacheModule.loadBuffer = responseNext;
  CPUstate.DCacheModule.cacheRequestBuffer = parkNext;
  CPUstate.DCacheModule.busy = busyNext;
  CPUstate.DCacheModule.phase = phaseNext;
  CPUstate.DCacheModule.hitCount = hitsNext;
  CPUstate.DCacheModule.missCount = missesNext;
  CPUstate.DCacheModule.flushSet = flushSetNext;
  CPUstate.DCacheModule.flushWay = flushWayNext;
  CPUstate.DCacheModule.flushDone = input.flushRequested ? flushDoneNext : false;
  CPUstate.DCacheModule.flushWriteStarted = flushStartedNext;
}
