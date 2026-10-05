#pragma once
#include "IMEM.hpp"
#include "InstructBuffer.hpp"
#include "SRAM.hpp"
#include "common.hpp"
#include <cstdint>
#include <cstring>
struct CacheLine {
  bool valid;
  uint32_t tag; // logical width: ICACHE_TAG_WIDTH
};
struct systemState;
struct ICacheRefillSelection {
  bool valid = false;
  uint8_t slot = 0;
};
struct ICacheInput {
  SquashInfo squashDetect;
  FetchDecision fetchDecision;
  bool popConsume = false;
  LineReturn lineReturn;
  uint8_t refillSlot = 0; // Request-queue slot, not the SRAM row index.
  const SRAM<NUM_OF_ICACHE_SETS, ICACHE_LINE_BITS, RV32_WORD_BITS>::Input
      &InputICacheDataSRAMInput;
  ICacheInput(const auto &dataInput) : InputICacheDataSRAMInput(dataInput) {}
};
struct ICacheRequest {
  uint32_t raw_inst;
  int32_t PC;
  int32_t predictPC;
  uint8_t ckptId;
  bool valid = false;
};
class ICache {
private:
  CacheLine requestBlocks[NUM_OF_ICACHE_SETS];
  SRAM<NUM_OF_ICACHE_SETS, ICACHE_LINE_BITS, RV32_WORD_BITS> datas;
  ICacheRequest requestBuffer[REQUEST_CAP];
  uint8_t readIndex = 0;
  uint8_t readLaneIndex = 0;
  bool readValid = false;
  uint8_t head;
  uint8_t count;
  uint32_t hitCount = 0;
  uint32_t missCount = 0;
  void clear(systemState &CPUstate) const;
  void pop(systemState &CPUstate) const;
  void pushRequest(uint32_t pc, int32_t predictPC, uint8_t ckptId, systemState &CPUstate) const;
  // The previous synchronous read can bypass the request-slot register only
  // when its saved identity matches the occupied, not-yet-ready queue head.
  bool headReadReady() const {
    return count > 0 && !requestBuffer[head].valid && readValid &&
           readIndex == head;
  }

public:
  ICache() { std::memset(this, 0, sizeof((*this))); }
  bool isRequestFull() const { return count == REQUEST_CAP; }
  bool isReturnReady() const {
    return count > 0 && (requestBuffer[head].valid || headReadReady());
  }
  bool hit(uint32_t addr) const;
  ICacheRefillSelection selectRefill(const LineReturn &) const;
  uint32_t getHitCount() const { return hitCount; }
  uint32_t getMissCount() const { return missCount; }
  uint32_t returnRaw() const {
    return headReadReady() ? datas.readLane(readLaneIndex)
                           : requestBuffer[head].raw_inst;
  }
  int32_t returnPC() const { return requestBuffer[head].PC; }
  int32_t returnPredictPC() const { return requestBuffer[head].predictPC; }
  uint8_t returnCkptId() const { return requestBuffer[head].ckptId; }
  void tick(const ICacheInput &, systemState &);
};
