#pragma once
#include "SRAM.hpp"
#include "AXI.hpp"
#include "common.hpp"
#include <array>
#include <cstdint>

struct systemState;
struct DCachePark {
  MemRequest request{};
  uint8_t way = 0;
  int8_t miss = -1;
  bool active = false;
  bool ready = false;
  bool deferred = false;
  bool cancelled = false;
};
struct DCacheMiss {
  uint32_t line = 0;
  uint32_t victimTag = 0;
  uint8_t owner = 0;
  uint8_t way = 0;
  uint8_t fillWord = 0;
  bool valid = false;
  bool dirty = false;
  bool readSent = false;
  bool writeSent = false;
  bool writeDone = false;
  bool responded = false;
};

// Comb-only events and physical SRAM ports, all derived from one old snapshot.
struct DCacheInput {
  MemDispatchDecision decision{};
  SquashInfo squashDetect{};
  int8_t lookup = -1;
  int8_t lookupCandidate = -1;
  int8_t response = -1;
  int8_t store = -1;
  int8_t mergeSlot = -1;
  int8_t mergeMiss = -1;
  int8_t allocate = -1;
  int8_t fillMiss = -1;
  int8_t writeAck = -1;
  bool fillBeat = false;
  bool fillFire = false;
  bool acceptLookup = false;
  bool exitValid = false;
  uint32_t exitValue = 0;
  uint32_t readHitRaw = 0; // One synchronous read result, shared by all consumers.
  LoadResponse loadResp{};
  DMEMRequest memoryRequest{};
  std::array<std::array<SRAM<NUM_OF_DCACHE_SETS, 32, 8>::Input,
                        DCACHE_WORDS_PER_LINE>, NUM_OF_DCACHE_WAYS>
      InputDCacheDataSRAMInput{};
  std::array<SRAM<NUM_OF_DCACHE_SETS, DCACHE_META_BITS>::Input,
             NUM_OF_DCACHE_WAYS> InputDCacheMetadataSRAMInput{};
  const AXIData &DMEMModule;
  DCacheInput(const AXIData &dmem) : DMEMModule(dmem) {}
};

// Eight request credits INCLUDE four MSHR owners. Different sets may have independent
// misses in flight. A reserved set stays locked until its complete refill;
// same-line Load waiters join before the first refill beat. Stores stay ordered.
class DCache {
  SRAM<NUM_OF_DCACHE_SETS, 32, 8>
      datas[NUM_OF_DCACHE_WAYS][DCACHE_WORDS_PER_LINE];
  SRAM<NUM_OF_DCACHE_SETS, DCACHE_META_BITS> metadata[NUM_OF_DCACHE_WAYS];
  bool validBits[NUM_OF_DCACHE_SETS][NUM_OF_DCACHE_WAYS]{};
  uint8_t plru[NUM_OF_DCACHE_SETS]{};
  DCachePark RequestBuffer[REQUEST_CAP]{};
  DCacheMiss misses[DCACHE_MSHR_CAP]{};
  uint32_t rawPayload[REQUEST_CAP]{};
  std::array<uint32_t, DCACHE_WORDS_PER_LINE> victimPayload[DCACHE_MSHR_CAP]{};
  // Bit j in row i means i was accepted before j. ROB tags cannot order
  // committed Stores that may outlive the current ROB window.
  uint8_t olderMask[REQUEST_CAP]{};
  uint8_t count = 0;
  uint8_t cursor = 0;
  uint8_t readIndex = 0;
  uint8_t readWayMask = 0;
  uint8_t readOffset = 0, readBytes = 0;
  bool readValid = false;
  uint64_t hitCount = 0;
  uint64_t missCount = 0;

  uint8_t activeMask() const;
  int oldestRequest(uint8_t mask) const;
  int oldestMiss(uint8_t mask) const;
  int freeRequest() const;
  int findMiss(uint32_t line) const;
  int freeMiss() const;
  bool setLocked(uint32_t address) const;
  bool storeHazard(int slot, bool allocatingMiss) const;
  bool victimHazard(int miss) const;
  int hitWay() const;
  uint8_t selectVictim(uint32_t set) const;
  uint32_t sramRaw(int slot, int way) const;
  uint32_t memoryRaw(int slot, const AXIData &dmem) const;
  uint32_t victimAddress(int miss) const;

public:
  DCache() = default;
  bool isRequestFull() const { return count == REQUEST_CAP; }
  bool isEmpty() const { return count == 0; }
  uint64_t getHitCount() const { return hitCount; }
  uint64_t getMissCount() const { return missCount; }
  void plan(DCacheInput &) const;
  void tick(const DCacheInput &, systemState &);
};
