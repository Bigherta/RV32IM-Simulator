#include "../include/DCache.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

void DCache::snapshotFrom(const DCache &other) {
  // Only pipeline state is copied -- the line array stays outside the
  // snapshot (never memcpy'd): cacheSets is heap-sized state owned by the
  // active instance only (reorder diff compares CPUstate live values).
  busy = other.busy;
  phase = other.phase;
  request = other.request;
  loadBuffer = other.loadBuffer;
  cacheRequestBuffer = other.cacheRequestBuffer;
  hitCount = other.hitCount;
  missCount = other.missCount;
}

DCacheProbe DCache::sampleProbe(const MemDispatchDecision &decision) const {
  DCacheProbe probe{};
  if (phase == Phase::READY && !decision.valid) return probe;
  const auto addr = phase == Phase::READY ? decision.request.address : cacheRequestBuffer.request.address;
  probe.setIndex = (addr >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
  probe.oldSet = cacheSets[probe.setIndex];
  for (int i = 0; i < NUM_OF_DCACHE_WAYS; ++i) {
    const auto &line = probe.oldSet.lines[i];
    if (line.valid && line.tag == (addr >> DCACHE_TAG_SHIFT)) {
      probe.hit = true;
      probe.way = static_cast<uint8_t>(i);
    }
  }
  if (!probe.hit) probe.way = AllocateLine(probe.setIndex);
  return probe;
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

/**
 * Processor read.
 * @param addr physical address.
 * @return data value.
 */
bool DCache::PrRd(uint32_t addr, int n_bytes, bool isSigned, int32_t &value) {
  // A request must never straddle a cache line: offset+width within the block.
  assert((addr & DCACHE_OFFSET_MASK) + n_bytes <= DCACHE_BLOCK_CAP);
  auto block_num = addr >> DCACHE_OFFSET_BITS;
  auto set_index = block_num & DCACHE_INDEX_MASK;
  auto tag = addr >> DCACHE_TAG_SHIFT;
  auto &cacheSet = cacheSets[set_index];
  bool hit = false;
  uint8_t hitIndex = 0;
  for (int i = 0; i < cacheSet.lines.size(); i++) {
    if (cacheSet.lines[i].tag == tag && cacheSet.lines[i].valid) {
      hit = true;
      hitIndex = i;
    }
  }
  if (hit && cacheSet.lines[hitIndex].valid) {
    if constexpr (NUM_OF_DCACHE_WAYS > 1) {
      // tree-PLRU update: 0=left,1=right, invert to root
      uint8_t &plru = cacheSet.plru;
      if (hitIndex < 2) plru |= 0x4; else plru &= ~0x4;
      if (hitIndex == 0) plru |= 0x2; else if (hitIndex == 1) plru &= ~0x2;
      else if (hitIndex == 2) plru |= 0x1; else plru &= ~0x1;
    }
    uint32_t rawData = 0;
    for (int i = 0; i < 4; ++i) {
      if (i < n_bytes) {
        rawData |= static_cast<uint32_t>(cacheSet.lines[hitIndex].datas[
                       (addr & DCACHE_OFFSET_MASK) + i])
                   << (i << 3);
      }
    }
    // sign-extend sub-word signed loads (mask branch identical to
    // DMEM::load_n_bytes): a bare static_cast<int32_t> would leave the
    // high bits zero for n<4 signed reads.
    if (isSigned && n_bytes == 1 && (rawData & 0x80u))
      rawData |= 0xFFFFFF00u;
    else if (isSigned && n_bytes == 2 && (rawData & 0x8000u))
      rawData |= 0xFFFF0000u;
    value = static_cast<int32_t>(rawData);
    return true;
  } else {
    auto distributeWay = AllocateLine(set_index);
    if (!cacheRequestBuffer.valid) {
      cacheRequestBuffer.request.address = addr;
      cacheRequestBuffer.request.isSigned = isSigned;
      cacheRequestBuffer.request.n_bytes = n_bytes;
      cacheRequestBuffer.request.op = Operation::Load;
      cacheRequestBuffer.valid = true;
      cacheRequestBuffer.targetWay = distributeWay;
    }
    request.readValid = true;
    request.read.address = addr & ~DCACHE_OFFSET_MASK;
    request.read.remainCycle = MEM_LATENCY;
    if (cacheSet.lines[distributeWay].dirty) {
      request.writeValid = true;
      // Victim base address must be rebuilt from the VICTIM line's own tag,
      // not the incoming request's tag -- otherwise the dirty data lands on
      // the wrong frame and the refetch below reads back stale memory.
      request.write.address =
          ((cacheSet.lines[distributeWay].tag << DCACHE_INDEX_BITS) +
           set_index)
          << DCACHE_OFFSET_BITS;
      std::memcpy(request.write.lineData, cacheSet.lines[distributeWay].datas,
                  DCACHE_BLOCK_CAP);
      request.write.remainCycle = MEM_LATENCY;
    }
    return false;
  }
}

/**
 * Processor write.
 * @param addr physical address.
 * @param val value to write.
 */
bool DCache::PrWr(uint32_t addr, uint32_t val, int n_bytes) {
  // A request must never straddle a cache line: offset+width within the block.
  assert((addr & DCACHE_OFFSET_MASK) + n_bytes <= DCACHE_BLOCK_CAP);
  auto block_num = addr >> DCACHE_OFFSET_BITS;
  auto set_index = block_num & DCACHE_INDEX_MASK;
  auto tag = addr >> DCACHE_TAG_SHIFT;
  auto &cacheSet = cacheSets[set_index];
  bool hit = false;
  uint8_t hitIndex = 0;
  for (int i = 0; i < cacheSet.lines.size(); i++) {
    if (cacheSet.lines[i].tag == tag && cacheSet.lines[i].valid) {
      hit = true;
      hitIndex = i;
    }
  }
  if (hit && cacheSet.lines[hitIndex].valid) {
    if constexpr (NUM_OF_DCACHE_WAYS > 1) {
      // tree-PLRU update: 0=left,1=right, invert to root
      uint8_t &plru = cacheSet.plru;
      if (hitIndex < 2) plru |= 0x4; else plru &= ~0x4;
      if (hitIndex == 0) plru |= 0x2; else if (hitIndex == 1) plru &= ~0x2;
      else if (hitIndex == 2) plru |= 0x1; else plru &= ~0x1;
    }
    cacheSet.lines[hitIndex].dirty = true;
    for (int i = 0; i < 4; ++i) {
      if (i < n_bytes && addr + i < MEM_SIZE) {
        cacheSet.lines[hitIndex].datas[(addr & DCACHE_OFFSET_MASK) + i] =
            (val >> (i << 3)) & 0xFF;
      }
    }
    return true;
  } else {
    auto distributeWay = AllocateLine(set_index);
    if (!cacheRequestBuffer.valid) {
      cacheRequestBuffer.request.address = addr;
      cacheRequestBuffer.request.n_bytes = n_bytes;
      cacheRequestBuffer.request.op = Operation::Store;
      cacheRequestBuffer.valid = true;
      cacheRequestBuffer.targetWay = distributeWay;
      cacheRequestBuffer.request.value = val;
    }
    request.readValid = true;
    request.read.address = addr & ~DCACHE_OFFSET_MASK;
    request.read.remainCycle = MEM_LATENCY;
    if (cacheSet.lines[distributeWay].dirty) {
      request.writeValid = true;
      // Victim base address must be rebuilt from the VICTIM line's own tag,
      // not the incoming request's tag -- otherwise the dirty data lands on
      // the wrong frame and the refetch below reads back stale memory.
      request.write.address =
          ((cacheSet.lines[distributeWay].tag << DCACHE_INDEX_BITS) +
           set_index)
          << DCACHE_OFFSET_BITS;
      std::memcpy(request.write.lineData, cacheSet.lines[distributeWay].datas,
                  DCACHE_BLOCK_CAP);
      request.write.remainCycle = MEM_LATENCY;
    }
    return false;
  }
}

void DCache::tick(const DCacheInput &input, systemState &CPUstate) {
  DMEMRequest requestNext{};
  LoadResponse responseNext{};
  DCachePark parkNext = cacheRequestBuffer;
  bool busyNext = busy;
  auto phaseNext = phase;
  uint64_t hitsNext = hitCount, missesNext = missCount;
  const auto &probe = input.probe;
  auto extract = [](const uint8_t *bytes, const MemRequest &req) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
      if (i < req.n_bytes) value |= uint32_t(bytes[(req.address & DCACHE_OFFSET_MASK) + i]) << (i << 3);
    if (req.isSigned && req.n_bytes == 1 && (value & 0x80u)) value |= 0xFFFFFF00u;
    if (req.isSigned && req.n_bytes == 2 && (value & 0x8000u)) value |= 0xFFFF0000u;
    return static_cast<int32_t>(value);
  };
  auto plruAfter = [](uint8_t old, uint8_t way) {
    uint8_t result = old;
    if (way < 2) result |= 4; else result &= ~4;
    if (way == 0) result |= 2; else if (way == 1) result &= ~2;
    else if (way == 2) result |= 1; else result &= ~1;
    return result;
  };
  if (phase == Phase::READY && input.decision.valid) {
    assert(!input.DMEMModule.isReadBusy() && !input.DMEMModule.isWriteBusy());
    const auto &req = input.decision.request;
    assert((req.address & DCACHE_OFFSET_MASK) + req.n_bytes <= DCACHE_BLOCK_CAP);
    const auto &oldLine = probe.oldSet.lines[probe.way];
    if (probe.hit) {
      ++hitsNext;
      if (req.op == Operation::Load) {
        const auto value = extract(oldLine.datas, req);
#ifdef _DEBUG
        if (!oldLine.dirty) assert(value == input.referenceValue);
#endif
        responseNext = {true, req.memIndex, req.robTag, value};
      } else {
        CPUstate.DCacheModule.cacheSets[probe.setIndex].lines[probe.way].dirty = true;
        for (int i = 0; i < 4; ++i)
          if (i < req.n_bytes && req.address + i < MEM_SIZE)
            CPUstate.DCacheModule.cacheSets[probe.setIndex].lines[probe.way].datas[(req.address & DCACHE_OFFSET_MASK) + i] = (uint32_t(req.value) >> (i << 3)) & 255u;
      }
      if constexpr (NUM_OF_DCACHE_WAYS > 1)
        CPUstate.DCacheModule.cacheSets[probe.setIndex].plru = plruAfter(probe.oldSet.plru, probe.way);
    } else {
      ++missesNext;
      parkNext = {req, true, probe.way};
      busyNext = true;
      phaseNext = Phase::WAIT;
      requestNext.readValid = true;
      requestNext.read.address = req.address & ~DCACHE_OFFSET_MASK;
      requestNext.read.remainCycle = MEM_LATENCY;
      if (oldLine.dirty) {
        requestNext.writeValid = true;
        requestNext.write.address = ((oldLine.tag << DCACHE_INDEX_BITS) | probe.setIndex) << DCACHE_OFFSET_BITS;
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
        if (i < req.n_bytes && req.address + i < MEM_SIZE)
          lineNext.datas[(req.address & DCACHE_OFFSET_MASK) + i] = (uint32_t(req.value) >> (i << 3)) & 255u;
    } else responseNext = {true, req.memIndex, req.robTag, extract(lineNext.datas, req)};
    CPUstate.DCacheModule.cacheSets[probe.setIndex].lines[way] = lineNext;
    if constexpr (NUM_OF_DCACHE_WAYS > 1)
      CPUstate.DCacheModule.cacheSets[probe.setIndex].plru = plruAfter(probe.oldSet.plru, way);
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
}
