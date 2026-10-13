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
    if (i < readBytes) {
      const uint32_t lane = readLaneIndex + i;
      raw |= datas[lane >> 2].readLane(lane & 3) << (i << 3);
    }
  }
  return raw;
}

void DCache::tick(const DCacheInput &input, systemState &CPUstate) {
  static_assert(REQUEST_CAP > 0 && REQUEST_CAP <= 128 &&
                (REQUEST_CAP & (REQUEST_CAP - 1)) == 0);
  static_assert(DCACHE_WORDS_PER_LINE > 0 &&
                DCACHE_WORDS_PER_LINE <= DCACHE_BLOCK_CAP);
  // All eligibility comes from old state/Input; never read the write target.
  // A refill beat writes a pair of banks; only the last beat completes.
  if (input.fillBeat)
    assert(canFill(input.DMEMModule));
  if (input.fillFire)
    assert(input.fillBeat && fillIsLast() && canFill(input.DMEMModule));
  const bool readConsumed = input.popConsume && headReadReady();
  if (input.popConsume) {
    assert(isReturnReady() || (input.fillFire && canFill(input.DMEMModule)));
    pop(CPUstate);
    CPUstate.DCacheModule.memoryReadSent = false;
    CPUstate.DCacheModule.memoryWriteSent = false;
    CPUstate.DCacheModule.fillWord = 0;
  } else {
    if (input.memoryRequest.readValid && !input.DMEMModule.isReadBusy() &&
        !input.DMEMModule.isReplyReady())
      CPUstate.DCacheModule.memoryReadSent = true;
    if (input.memoryRequest.writeValid && !input.DMEMModule.isWriteBusy())
      CPUstate.DCacheModule.memoryWriteSent = true;
    if (input.fillBeat) {
      assert(!fillIsLast());
      CPUstate.DCacheModule.fillWord = fillWord + DCACHE_FILL_WORDS_PER_BEAT;
    }
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
          readLineValid ? (meta & DCACHE_META_TAG_MASK) : 0u;
      CPUstate.DCacheModule.RequestBuffer[readIndex].lineValid =
          readLineValid;
      CPUstate.DCacheModule.RequestBuffer[readIndex].lineDirty =
          readLineValid && (meta & DCACHE_META_DIRTY) != 0;
      if (hit) {
        CPUstate.DCacheModule.RequestBuffer[readIndex].raw = readRaw();
        CPUstate.DCacheModule.RequestBuffer[readIndex].valid = true;
      } else {
        CPUstate.DCacheModule.RequestBuffer[readIndex].needReadMemory = true;
        for (int i = 0; i < DCACHE_BLOCK_CAP; ++i)
          CPUstate.DCacheModule.victimLine[i] =
              datas[i >> 2].readLane(i & 3);
      }
    }
  }

  const auto &dataInputs = input.InputDCacheDataSRAMInput;
  const auto &metaInput = input.InputDCacheMetadataSRAMInput;
  if (input.fillFire) {
    const auto set = (headAddr() >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
    CPUstate.DCacheModule.validBits[set] = true;
  }
  if (input.fillBeat) {
    assert((fillWord & (DCACHE_FILL_WORDS_PER_BEAT - 1)) == 0);
    assert(fillWord + DCACHE_FILL_WORDS_PER_BEAT <= DCACHE_WORDS_PER_LINE);
    for (int b = 0; b < DCACHE_WORDS_PER_LINE; ++b) {
      const bool selected = b >= fillWord &&
                            b < fillWord + DCACHE_FILL_WORDS_PER_BEAT;
      assert(dataInputs[b].enable == selected);
      assert(!selected || dataInputs[b].writeEnable);
      for (int lane = 0; lane < RV32_WORD_BYTES; ++lane)
        assert(!selected || dataInputs[b].writeMask[lane]);
    }
    assert(metaInput.enable == input.fillFire);
    assert(!input.fillFire || (metaInput.writeEnable && input.popConsume));
  }
  bool allBanksRead = metaInput.enable && !metaInput.writeEnable;
  for (int b = 0; b < DCACHE_WORDS_PER_LINE; ++b)
    allBanksRead = allBanksRead && dataInputs[b].enable &&
                   !dataInputs[b].writeEnable;
  const bool readFire = allBanksRead;
  if (readFire) {
    assert(needsLookup());
    assert(!input.fillBeat);
    const auto &req = RequestBuffer[head].request;
    const auto set = (req.address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
    assert(metaInput.addr == set);
    for (int b = 0; b < DCACHE_WORDS_PER_LINE; ++b)
      assert(dataInputs[b].addr == set);
    CPUstate.DCacheModule.readIndex = head;
    CPUstate.DCacheModule.readLineValid = validBits[set];
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

  for (int b = 0; b < DCACHE_WORDS_PER_LINE; ++b)
    datas[b].tick(dataInputs[b], CPUstate.DCacheModule.datas[b]);
  metadata.tick(metaInput, CPUstate.DCacheModule.metadata);
  CPUstate.DCacheModule.readValid = readFire;
  CPUstate.DCacheModule.count = count +
                                static_cast<unsigned>(input.decision.valid) -
                                static_cast<unsigned>(input.popConsume);
}
