#include "../include/ICache.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cassert>
#include <cstdint>

void ICache::clear(systemState &CPUstate) const {
  // only clear the request queue; cache lines are non-speculative and kept
  for (int i = 0; i < REQUEST_CAP; ++i)
    CPUstate.ICacheModule.requestBuffer[i].valid = false;
  CPUstate.ICacheModule.head = 0;
  CPUstate.ICacheModule.count = 0;
  CPUstate.ICacheModule.readIndex = 0;
  CPUstate.ICacheModule.readLaneIndex = 0;
  CPUstate.ICacheModule.readValid = 0;
}

void ICache::pop(systemState &CPUstate) const {
  CPUstate.ICacheModule.requestBuffer[head].valid = false;
  CPUstate.ICacheModule.head = (head + 1) & (REQUEST_CAP - 1);
}

void ICache::pushRequest(uint32_t pc, int32_t predictPC, uint8_t ckptId, systemState &CPUstate) const {
  ICacheRequest request{};
  request.raw_inst = 0;
  request.PC = pc;
  request.predictPC = predictPC;
  request.ckptId = ckptId;
  request.valid = false;
  CPUstate.ICacheModule.requestBuffer[(head + count) & (REQUEST_CAP - 1)] = request;
}

bool ICache::hit(uint32_t addr) const {
  auto index = (addr >> ICACHE_OFFSET_BITS) & ICACHE_INDEX_MASK;
  return requestBlocks[index].valid &&
         (requestBlocks[index].tag ==
          (addr >> ICACHE_TAG_SHIFT));
}

ICacheRefillSelection ICache::selectRefill(const LineReturn &lineReturn) const {
  static_assert(REQUEST_CAP > 0 && (REQUEST_CAP & (REQUEST_CAP - 1)) == 0);
  ICacheRefillSelection selection{};
  // Scan the old occupied window in age order, not the physical slot order.
  // valid means ready; a pending SRAM hit must not be mistaken for a miss.
  for (int age = 0; age < REQUEST_CAP; ++age) {
    const auto slot = (head + age) & (REQUEST_CAP - 1);
    const auto &request = requestBuffer[slot];
    const uint32_t requestLine =
        static_cast<uint32_t>(request.PC) & ~ICACHE_OFFSET_MASK;
    if (lineReturn.valid && age < count && !selection.valid &&
        !request.valid && !(readValid && readIndex == slot) &&
        requestLine == lineReturn.lineAddr) {
      selection.valid = true;
      selection.slot = static_cast<uint8_t>(slot);
    }
  }
  return selection;
}

void ICache::tick(const ICacheInput &input, systemState &CPUstate) {
  // stage 3 flush: clear the speculative request queue (cache lines kept)
  if (input.squashDetect.needSquash) {
    clear(CPUstate);
    return;
  }
  // stage 2 pop: self-release once FQ consumed the ICache head (write-own-only)
  const bool readConsumed = input.popConsume && headReadReady();
  if (input.popConsume) {
    assert(isReturnReady());
    pop(CPUstate);
  }
  // stage 2 line refill: consume the accepted IMEM line-return bus, fill the
  // cache line and backfill the selected placeholder entry
  if (input.lineReturn.valid) {
    assert(input.refillSlot < REQUEST_CAP);
    auto cachelineIndex =
        (input.lineReturn.lineAddr >> ICACHE_OFFSET_BITS) & ICACHE_INDEX_MASK;
    CPUstate.ICacheModule.requestBlocks[cachelineIndex].valid = true;
    CPUstate.ICacheModule.requestBlocks[cachelineIndex].tag =
        input.lineReturn.lineAddr >>
        ICACHE_TAG_SHIFT;
    // The combinational selector chose one live miss from the old snapshot.
    // It cannot be the ready head popped above or the new request pushed below.
    const auto slot = input.refillSlot;
    const uint32_t pc = static_cast<uint32_t>(requestBuffer[slot].PC);
    CPUstate.ICacheModule.requestBuffer[slot].raw_inst =
        input.lineReturn.data[(pc >> RV32_WORD_BYTE_BITS) & ICACHE_WORD_INDEX_MASK];
    CPUstate.ICacheModule.requestBuffer[slot].valid = true;
  }
  // stage 1 push: both hits and misses occupy an unready placeholder.
  if (input.fetchDecision.valid) {
    bool isHit = hit(input.fetchDecision.pc);
    // host-only: accepted-fetch classification for cross-tree trace diagnosis.
    if (debug::enabled(debug::TOPIC_MEM))
      debug::print("IC_ACCEPT idx=%u pc=%08x hit=%u\n", hitCount + missCount,
                   input.fetchDecision.pc, static_cast<unsigned>(isHit));
    if (isHit) {
      CPUstate.ICacheModule.readIndex = (head + count) & (REQUEST_CAP - 1);
      CPUstate.ICacheModule.readLaneIndex =
          (input.fetchDecision.pc >> RV32_WORD_BYTE_BITS) & ICACHE_WORD_INDEX_MASK;
    }
    pushRequest(input.fetchDecision.pc,
                                      input.fetchDecision.predictedPC,
                                       input.fetchDecision.ckptId, CPUstate);
    if (isHit)
      CPUstate.ICacheModule.hitCount = hitCount + 1;
    else
      CPUstate.ICacheModule.missCount = missCount + 1;
  }

  // A head read already delivered through the return interface must not
  // resurrect its popped slot. Backpressured/younger reads still land here.
  if (readValid && !readConsumed) {
    CPUstate.ICacheModule.requestBuffer[readIndex].valid = true;
    CPUstate.ICacheModule.requestBuffer[readIndex].raw_inst =
        datas.readLane(readLaneIndex);
  }
  datas.tick(input.InputICacheDataSRAMInput, CPUstate.ICacheModule.datas);
  CPUstate.ICacheModule.readValid =
      input.fetchDecision.valid && hit(input.fetchDecision.pc) &&
              input.InputICacheDataSRAMInput.enable &&
              !input.InputICacheDataSRAMInput.writeEnable
          ? true
          : false;
  CPUstate.ICacheModule.count = count + static_cast<unsigned>(input.fetchDecision.valid) -
                               static_cast<unsigned>(input.popConsume);
}
