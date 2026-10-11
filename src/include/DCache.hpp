#pragma once
#include "DMEM.hpp"
#include "ROB.hpp"
#include "common.hpp"
#include <array>
#include <cstdint>
#include <cstring>

struct systemState;
struct DCachePark {
  MemRequest request; // op/addr/value/nBytes/isSigned/robTag/memIndex
  bool valid = false;
  uint8_t targetWay = 0; // AllocateLine 结果
};
struct Cacheline {
  bool valid = false;
  bool dirty = false;
  uint8_t datas[DCACHE_BLOCK_CAP];
  uint32_t tag = 0;
};
struct CacheSet {
  std::array<Cacheline, NUM_OF_DCACHE_WAYS> lines;
  uint8_t plru = 0; // tree-PLRU: b2=root, b1=left, b0=right; 0=left,1=right
};
struct DCacheSetSnapshot {
  CacheSet oldSet{};
  uint32_t setIndex = 0;
  uint8_t way = 0;
  bool hit = false;
};
struct DCacheInput {
  bool flushRequested = false; // Whole-cache writeback, after CPU transactions drain.
  MemDispatchDecision decision;
  DCacheSetSnapshot setSnapshot; // Sampled by comb(), never from the write target in tick().
  // Snapshot reference to the downstream memory: the DCache observes DMEM's
  // completion through the comb-refreshed snapshot. DMEM::tick only ever
  // writes its own CPUstate members.
  const DMEM &DMEMModule;
  DCacheInput(const DMEM &dmem) : DMEMModule(dmem) {}
};

// Parameterized data cache / direct mapping or tree-PLRU / write-back + write-allocate /
// dual-port DMEM. Geometry is defined in common.hpp.
// Hard constraints:
//  * isBusy() == "a request is in flight" -- arbiter never issues while busy.
//  * When !isBusy() the DCache must UNCONDITIONALLY accept the decision: the
//    store was already popped from the SQ in this cycle.
//  * The whole line array NEVER lives in the comb snapshot; comb samples only
//    the selected old set for tick() through sampleSet().
class DCache {
private:
  enum class Phase : uint8_t { READY, WAIT, FLUSH_SCAN, FLUSH_WAIT };
  bool busy = false;
  Phase phase = Phase::READY;
  std::array<CacheSet, NUM_OF_DCACHE_SETS> cacheSets;
  DCachePark cacheRequestBuffer;
  LoadResponse loadBuffer;
  DMEMRequest request;
  uint64_t hitCount = 0;
  uint64_t missCount = 0;
  uint32_t flushSet = 0;
  uint8_t flushWay = 0;
  bool flushDone = false;
  bool flushWriteStarted = false;

public:
  // Deterministic initial state, including data in invalid lines. This class
  // is trivially copyable, as required by the snapshot model.
  DCache() { std::memset(this, 0, sizeof(*this)); }
  bool isBusy() const { return busy; }
  bool isIdle() const { return phase == Phase::READY && !request.readValid && !request.writeValid; }
  bool isFlushDone() const { return flushDone; }
  bool hasLoadResponse() const { return loadBuffer.valid; }
  const DMEMRequest &forwardRequest() const { return request; }
  // D$ access statistics: one count per accepted mem decision (load or store,
  // hit == line present, miss == refill started). Debug/report only.
  uint64_t getHitCount() const { return hitCount; }
  uint64_t getMissCount() const { return missCount; }
  LoadResponse loadResp(const SquashInfo &squash) const {
    LoadResponse resp{};
    if (loadBuffer.valid &&
        (!squash.needSquash ||
         ROB::isOlder(loadBuffer.robTag, squash.SquashTag))) {
      resp = loadBuffer;
    }
    return resp;
  }
  uint8_t AllocateLine(int set_idx)
      const; // distribute the line without modifying anything
  void tick(const DCacheInput &, systemState &);
  void snapshotFrom(const DCache &other);
  DCacheSetSnapshot sampleSet(const MemDispatchDecision &) const;
};
