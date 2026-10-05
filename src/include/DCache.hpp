#pragma once
#include "DMEM.hpp"
#include "ROB.hpp"
#include "common.hpp"
#include <array>
#include <cstdint>
#include <cstring>

struct systemState;
struct DCachePark {
  MemRequest request; // op/addr/value/n_bytes/isSigned/robTag/memIndex
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
struct DCacheProbe {
  CacheSet oldSet{};
  uint32_t setIndex = 0;
  uint8_t way = 0;
  bool hit = false;
};
struct DCacheInput {
  MemDispatchDecision decision;
  DCacheProbe probe; // Sampled by comb(), never from the write target in tick().
  int32_t referenceValue = 0; // host-only clean-line assertion input.
  // Snapshot reference to the downstream memory: the DCache observes DMEM's
  // completion through the comb-refreshed snapshot (order-independent under
  // reorder_test; DMEM::tick only ever writes its own CPUstate members).
  const DMEM &DMEMModule;
  DCacheInput(const DMEM &dmem) : DMEMModule(dmem) {}
};

// Parameterized data cache / direct mapping or tree-PLRU / write-back + write-allocate /
// dual-port DMEM. Geometry is defined in common.hpp.
// Hard constraints:
//  * isBusy() == "a request is in flight" -- arbiter never issues while busy.
//  * When !isBusy() the DCache must UNCONDITIONALLY accept the decision: the
//    store was already popped from the SQ in this cycle.
//  * The line array NEVER lives in the comb snapshot; comb only reads isBusy()/forwardRequest().
class DCache {
private:
  enum class Phase : uint8_t { READY, WAIT };
  bool busy = false;
  Phase phase = Phase::READY;
  std::array<CacheSet, NUM_OF_DCACHE_SETS> cacheSets;
  DCachePark cacheRequestBuffer;
  LoadResponse loadBuffer;
  DMEMRequest request;
  uint64_t hitCount = 0;
  uint64_t missCount = 0;

public:
  // Same whole-object zero-init as ICache: without this, cacheSets[] lines
  // that are never filled carry uninitialized datas[] (never-written stack
  // bytes), which made the reorder_test dcache digest garbage-dependent
  // (valid=false lines were hashed too). Whole-object memset also pins any
  // future member added without an NSDMI. Object is trivially copyable POD,
  // so memset(this) is safe here (ICache already does the same).
  DCache() { std::memset(this, 0, sizeof(*this)); }
  bool isBusy() const { return busy; }
  const DMEMRequest &forwardRequest() const { return request; }
  // D$ access statistics: one count per accepted mem decision (load or store,
  // hit == line present, miss == refill started). Debug/report only.
  uint64_t getHitCount() const { return hitCount; }
  uint64_t getMissCount() const { return missCount; }
  bool PrRd(uint32_t addr, int n_bytes, bool isSigned, int32_t &value);
  bool PrWr(uint32_t addr, uint32_t val, int n_bytes);
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
  DCacheProbe sampleProbe(const MemDispatchDecision &) const;
};
