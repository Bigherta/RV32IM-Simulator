#pragma once
#include "InstructBuffer.hpp"
#include "IMEM.hpp"
#include "common.hpp"
#include <cstdint>
#include <cstring>
struct CacheLine {
  bool valid;
  uint32_t tag; // real: 18 bit for tag
  uint32_t data[ICACHE_BLOCK_CAP >> 2]; // 16B line = 4x32-bit words
};
struct systemState;
struct ICacheInput {
  SquashInfo squashDetect;
  FetchDecision fetchDecision;
  bool popConsume = false;
  LineReturn lineReturn;
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
  CacheLine blocks[ICACHE_CAP];
  ICacheRequest requestBuffer[REQUEST_CAP];
  uint8_t head;
  uint8_t count;
  uint32_t hitCount = 0;
  uint32_t missCount = 0;
  void clear();
  void pop();
  void pushRequest(uint32_t raw_inst, uint32_t pc, int32_t predictPC, uint8_t ckptId, bool valid);

public:
  ICache() { std::memset(this, 0, sizeof((*this))); }
  uint8_t getHead() const { return head; }
  bool isRequestFull() const { return count == REQUEST_CAP; }
  bool isReturnReady() const { return count > 0 && requestBuffer[head].valid; }
  bool hit(uint32_t addr) const;
  uint32_t getHitCount() const { return hitCount; }
  uint32_t getMissCount() const { return missCount; }
  uint32_t returnRaw() const { return requestBuffer[head].raw_inst; }
  int32_t returnPC() const { return requestBuffer[head].PC; }
  int32_t returnPredictPC() const { return requestBuffer[head].predictPC; }
  uint8_t returnCkptId() const { return requestBuffer[head].ckptId; }
  void tick(const ICacheInput &, systemState &);
};