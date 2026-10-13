#pragma once
#include "SRAM.hpp"
#include "DMEM.hpp"
#include "common.hpp"
#include <array>
#include <cstdint>

struct systemState;
struct DCachePark {
  MemRequest request; // op/addr/value/nBytes/isSigned/robTag/memIndex
  uint32_t raw = 0; // Little-endian bytes, without Load sign extension.
  uint32_t tag = 0; // Queried victim tag, not the incoming request tag.
  bool lineValid = false;
  bool lineDirty = false;
  bool needReadMemory = false;
  bool cancelled = false; // Accepted speculative Load cancelled by recovery.
  bool valid = false; // SRAM hit data ready; queue occupancy is head/count.
};
struct DCacheInput {
  MemDispatchDecision decision{};
  bool popConsume = false; // Load returned, Store written, or refill served.
  bool fillBeat = false;   // A refill beat writes a pair of banks this cycle.
  bool fillFire = false;   // Last refill beat: line completes this cycle.
  DMEMRequest memoryRequest{}; // Same accepted read/write events as DMEMInput.
  SquashInfo squashDetect{};
  // Enqueue and lookup are independent. Only the old head reads the SRAMs.
  // Data is banked by word; a refill beat enables two adjacent banks.
  std::array<SRAM<NUM_OF_DCACHE_SETS, 32, 8>::Input, DCACHE_WORDS_PER_LINE>
      InputDCacheDataSRAMInput{};
  SRAM<NUM_OF_DCACHE_SETS, DCACHE_META_BITS>::Input
      InputDCacheMetadataSRAMInput{};
  const DMEM &DMEMModule;
  DCacheInput(const DMEM &dmem) : DMEMModule(dmem) {}
};

// Direct-mapped, write-back/write-allocate cache. Requests queue independently
// of the single-port SRAMs; only the head is serviced. One miss can use both
// DMEM ports. No HALT scan or drain is implemented here.
class DCache {
private:
  SRAM<NUM_OF_DCACHE_SETS, 32, 8> datas[DCACHE_WORDS_PER_LINE];
  SRAM<NUM_OF_DCACHE_SETS, DCACHE_META_BITS> metadata;
  bool validBits[NUM_OF_DCACHE_SETS]{};
  DCachePark RequestBuffer[REQUEST_CAP]{};
  uint8_t head = 0;
  uint8_t count = 0;
  uint8_t readIndex = 0;
  uint8_t readLaneIndex = 0;
  uint8_t readBytes = 0;
  bool readValid = false;
  bool readLineValid = false; // Sampled with the synchronous SRAM read.
  bool memoryReadSent = false;
  bool memoryWriteSent = false;
  uint8_t fillWord = 0; // First bank of the next refill beat (0,2,...,14).
  std::array<uint8_t, DCACHE_BLOCK_CAP> victimLine{};
  uint64_t hitCount = 0;
  uint64_t missCount = 0;
  void pop(systemState &CPUstate) const;
  void pushRequest(const MemRequest &request, systemState &CPUstate) const;
  uint32_t readRaw() const;

public:
  // Cold host storage is deterministic; no SRAM initialization transaction.
  // Valid/control registers start clear; this class remains trivially copyable.
  DCache() = default;
  bool isEmpty() const { return count == 0; }
  bool isRequestFull() const { return count == REQUEST_CAP; }
  bool canAccept(bool popConsume) const {
    return !isRequestFull() || (popConsume && !isEmpty());
  }
  bool needsLookup() const {
    return !isEmpty() && !readValid && !RequestBuffer[head].valid &&
           !RequestBuffer[head].needReadMemory;
  }
  // Both result storage and head bypass use the same old-row hit predicate.
  bool readHit() const {
    return readValid && readLineValid &&
           (RequestBuffer[readIndex].request.address >> DCACHE_TAG_SHIFT) ==
               (metadata.readLane(0) & DCACHE_META_TAG_MASK);
  }
  // SRAM hit data belongs only to its saved occupied, not-yet-ready slot.
  bool headReadReady() const {
    return count > 0 && !RequestBuffer[head].valid && readValid &&
           readIndex == head && readHit();
  }
  bool isHeadRequestStore() const {
    return !isEmpty() && RequestBuffer[head].request.op == Operation::Store;
  }
  bool isHeadCancelled() const {
    return !isEmpty() && RequestBuffer[head].cancelled;
  }
  uint32_t headValue() const {
    return RequestBuffer[head].request.value;
  }
  uint32_t headAddr() const {
    return RequestBuffer[head].request.address;
  }
  uint8_t headBytes() const {
    return RequestBuffer[head].request.nBytes;
  }
  bool headReadMemory() const {
    return !isEmpty() && RequestBuffer[head].needReadMemory;
  }
  bool headDirty() const {
    return !isEmpty() && RequestBuffer[head].lineValid &&
           RequestBuffer[head].lineDirty;
  }
  bool needsMemoryRead() const {
    return headReadMemory() && !memoryReadSent;
  }
  bool needsMemoryWrite() const {
    return headReadMemory() && headDirty() && !memoryWriteSent;
  }
  bool canFill(const DMEM &dmem) const {
    return headReadMemory() && memoryReadSent && dmem.isReplyReady() &&
           (!headDirty() || (memoryWriteSent && !dmem.isWriteBusy()));
  }
  uint8_t fillWordIndex() const { return fillWord; }
  bool fillIsLast() const {
    return fillWord == DCACHE_WORDS_PER_LINE - DCACHE_FILL_WORDS_PER_BEAT;
  }
  uint32_t victimAddr() const {
    const auto set = (headAddr() >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
    return ((RequestBuffer[head].tag << DCACHE_INDEX_BITS) | set)
           << DCACHE_OFFSET_BITS;
  }
  uint8_t victimByte(int i) const {
    return victimLine[i];
  }
  bool isReturnReady() const {
    return count > 0 && (RequestBuffer[head].valid || headReadReady());
  }
  const MemRequest &returnRequest() const { return RequestBuffer[head].request; }
  uint32_t returnRaw() const {
    return headReadReady() ? readRaw() : RequestBuffer[head].raw;
  }
  uint32_t returnTag() const {
    return headReadReady() ? (metadata.readLane(0) & DCACHE_META_TAG_MASK)
                           : RequestBuffer[head].tag;
  }
  bool returnLineValid() const {
    return headReadReady() ? readLineValid
                           : RequestBuffer[head].lineValid;
  }
  bool returnLineDirty() const {
    return headReadReady() ? (metadata.readLane(0) & DCACHE_META_DIRTY) != 0
                           : RequestBuffer[head].lineDirty;
  }
  uint64_t getHitCount() const { return hitCount; }
  uint64_t getMissCount() const { return missCount; }
  void tick(const DCacheInput &, systemState &);
};
