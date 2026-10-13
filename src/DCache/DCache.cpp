#include "../include/DCache.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include "ROB.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

void DCache::pop(systemState &CPUstate) const {
  CPUstate.DCacheModule.RequestBuffer[head].valid = false;
  CPUstate.DCacheModule.RequestBuffer[head].needReadMemory = false;
  CPUstate.DCacheModule.head = (head + 1) & (REQUEST_CAP - 1);
}

void DCache::pushRequest(const MemRequest &request,
                         systemState &CPUstate) const {
  DCachePark entry{};
  entry.request = request;
  CPUstate.DCacheModule.RequestBuffer[(head + count) & (REQUEST_CAP - 1)] =
      entry;
}

uint32_t DCache::readRaw() const {
  uint32_t raw = 0;
  for (int i = 0; i < 4; ++i) {
    if (i < readBytes)
      raw |= datas.readLane(readLaneIndex + i) << (i << 3);
  }
  return raw;
}

void DCache::tick(const DCacheInput &input, systemState &CPUstate) {
  static_assert(REQUEST_CAP > 0 && REQUEST_CAP <= 128 &&
                (REQUEST_CAP & (REQUEST_CAP - 1)) == 0);
  // All eligibility comes from old state/Input; never read the write target.
  const bool readConsumed = input.popConsume && headReadReady();
  if (input.popConsume) {
    assert(isReturnReady() || (input.fillFire && canFill(input.DMEMModule)));
    pop(CPUstate);
    CPUstate.DCacheModule.memoryReadSent = false;
    CPUstate.DCacheModule.memoryWriteSent = false;
  } else {
    if (input.memoryRequest.readValid && !input.DMEMModule.isReadBusy() &&
        !input.DMEMModule.isReplyReady())
      CPUstate.DCacheModule.memoryReadSent = true;
    if (input.memoryRequest.writeValid && !input.DMEMModule.isWriteBusy())
      CPUstate.DCacheModule.memoryWriteSent = true;
  }
  // A read's metadata/data are used only in its return cycle. Miss payload is
  // saved before any later SRAM access, including writes with undefined output.
  if (readValid) {
    assert(!isEmpty() && readIndex == head);
    const bool hit = readHit();
    if (hit)
      CPUstate.DCacheModule.hitCount = hitCount + 1;
    else
      CPUstate.DCacheModule.missCount = missCount + 1;
    if (!readConsumed) {
      const auto meta = metadata.readLane(0);
      CPUstate.DCacheModule.RequestBuffer[readIndex].tag =
          meta & DCACHE_META_TAG_MASK;
      CPUstate.DCacheModule.RequestBuffer[readIndex].lineValid =
          (meta & DCACHE_META_VALID) != 0;
      CPUstate.DCacheModule.RequestBuffer[readIndex].lineDirty =
          (meta & DCACHE_META_DIRTY) != 0;
      if (hit) {
        CPUstate.DCacheModule.RequestBuffer[readIndex].raw = readRaw();
        CPUstate.DCacheModule.RequestBuffer[readIndex].valid = true;
      } else {
        CPUstate.DCacheModule.RequestBuffer[readIndex].needReadMemory = true;
        for (int i = 0; i < DCACHE_BLOCK_CAP; ++i)
          CPUstate.DCacheModule.victimLine[i] = datas.readLane(i);
      }
    }
  }

  const auto &dataInput = input.InputDCacheDataSRAMInput;
  const auto &metaInput = input.InputDCacheMetadataSRAMInput;
  const bool readFire = dataInput.enable && !dataInput.writeEnable &&
                        metaInput.enable && !metaInput.writeEnable;
  if (readFire) {
    assert(needsLookup());
    const auto &req = RequestBuffer[head].request;
    const auto set = (req.address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
    assert(dataInput.addr == set && metaInput.addr == set);
    CPUstate.DCacheModule.readIndex = head;
    CPUstate.DCacheModule.readLaneIndex = req.address & DCACHE_OFFSET_MASK;
    CPUstate.DCacheModule.readBytes = req.nBytes;
  }

  // Recovery must persist beyond the broadcast cycle: the same ROB/LQ
  // identity can be reallocated while this accepted read is still in flight.
  if (input.squashDetect.needSquash) {
    for (int age = 0; age < REQUEST_CAP; ++age) {
      const auto slot = (head + age) & (REQUEST_CAP - 1);
      const auto &entry = RequestBuffer[slot];
      if (age < count && !entry.cancelled &&
          entry.request.op == Operation::Load &&
          !ROB::isOlder(entry.request.robTag, input.squashDetect.SquashTag))
        CPUstate.DCacheModule.RequestBuffer[slot].cancelled = true;
    }
  }

  if (input.decision.valid) {
    assert(canAccept(input.popConsume));
    const auto &req = input.decision.request;
    assert(req.nBytes == 1 || req.nBytes == 2 || req.nBytes == 4);
    assert((req.address & DCACHE_OFFSET_MASK) + req.nBytes <= DCACHE_BLOCK_CAP);
    assert(req.op == Operation::Load || req.op == Operation::Store);
    // Allocation owns the last write on a reused queue slot.
    pushRequest(req, CPUstate);
  }

  datas.tick(input.InputDCacheDataSRAMInput, CPUstate.DCacheModule.datas);
  metadata.tick(metaInput, CPUstate.DCacheModule.metadata);
  CPUstate.DCacheModule.readValid = readFire;
  CPUstate.DCacheModule.count = count +
                                static_cast<unsigned>(input.decision.valid) -
                                static_cast<unsigned>(input.popConsume);
}
