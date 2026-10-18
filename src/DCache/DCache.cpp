#include "../include/DCache.hpp"
#include "../include/CPU.hpp"
#include "ROB.hpp"
#include <bit>
#include <cassert>

uint8_t DCache::activeMask() const {
  uint8_t mask = 0;
  for (int i = 0; i < REQUEST_CAP; ++i)
    if (RequestBuffer[i].active) mask |= static_cast<uint8_t>(1u << i);
  return mask;
}

int DCache::oldestRequest(uint8_t mask) const {
  int selected = -1;
  for (int i = 0; i < REQUEST_CAP; ++i) {
    bool prior = false;
    for (int j = 0; j < REQUEST_CAP; ++j)
      if ((mask & (1u << j)) && (olderMask[j] & (1u << i))) prior = true;
    if ((mask & (1u << i)) && !prior) selected = i;
  }
  return selected;
}

int DCache::oldestMiss(uint8_t mask) const {
  uint8_t owners = 0;
  for (int m = 0; m < DCACHE_MSHR_CAP; ++m)
    if (mask & (1u << m)) owners |= static_cast<uint8_t>(1u << misses[m].owner);
  const int owner = oldestRequest(owners);
  int selected = -1;
  for (int m = 0; m < DCACHE_MSHR_CAP; ++m)
    if ((mask & (1u << m)) && misses[m].owner == owner) selected = m;
  return selected;
}

int DCache::freeRequest() const {
  int selected = -1;
  for (int n = 0; n < REQUEST_CAP; ++n) {
    const int slot = (cursor + n) & (REQUEST_CAP - 1);
    if (selected < 0 && !RequestBuffer[slot].active) selected = slot;
  }
  return selected;
}

int DCache::findMiss(uint32_t line) const {
  int selected = -1;
  for (int m = 0; m < DCACHE_MSHR_CAP; ++m)
    if (misses[m].valid && misses[m].line == line) selected = m;
  return selected;
}

int DCache::freeMiss() const {
  int selected = -1;
  for (int m = 0; m < DCACHE_MSHR_CAP; ++m)
    if (selected < 0 && !misses[m].valid) selected = m;
  return selected;
}

bool DCache::setLocked(uint32_t address) const {
  const uint32_t set = (address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
  bool locked = false;
  for (int m = 0; m < DCACHE_MSHR_CAP; ++m)
    if (misses[m].valid &&
        ((misses[m].line >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK) == set)
      locked = true;
  return locked;
}

bool DCache::storeHazard(int slot, bool allocatingMiss) const {
  const auto &load = RequestBuffer[slot].request;
  const uint32_t set = (load.address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
  bool blocked = false;
  for (int s = 0; s < REQUEST_CAP; ++s) {
    if (RequestBuffer[s].active && RequestBuffer[s].request.op == Operation::Store &&
        (olderMask[s] & (1u << slot))) {
      const auto &store = RequestBuffer[s].request;
      // Cache requests never cross a line. Compare the line once and use
      // small byte offsets, not two full-address subtractors for each pair.
      const uint32_t lo = load.address & DCACHE_OFFSET_MASK;
      const uint32_t so = store.address & DCACHE_OFFSET_MASK;
      const bool overlap = (load.address >> DCACHE_OFFSET_BITS) == (store.address >> DCACHE_OFFSET_BITS) &&
          lo <= so + static_cast<uint32_t>(store.nBytes) - 1u &&
          so <= lo + static_cast<uint32_t>(load.nBytes) - 1u;
      // Hits may bypass disjoint bytes. A younger miss must not reserve the
      // set needed by an older Store; recheck it after the Store has executed.
      if (overlap || (allocatingMiss &&
          ((store.address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK) == set))
        blocked = true;
    }
  }
  return blocked;
}

uint32_t DCache::victimAddress(int miss) const {
  const auto &m = misses[miss];
  const uint32_t set = (m.line >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
  return ((m.victimTag << DCACHE_INDEX_BITS) | set) << DCACHE_OFFSET_BITS;
}

bool DCache::victimHazard(int miss) const {
  bool blocked = false;
  for (int other = 0; other < DCACHE_MSHR_CAP; ++other)
    if (other != miss && misses[other].valid && misses[other].dirty &&
        !misses[other].writeDone && victimAddress(other) == misses[miss].line)
      blocked = true;
  return blocked;
}

int DCache::hitWay() const {
  int selected = -1;
  if (readValid) {
    const uint32_t tag = RequestBuffer[readIndex].request.address >> DCACHE_TAG_SHIFT;
    for (int w = 0; w < NUM_OF_DCACHE_WAYS; ++w)
      if (selected < 0 && (readWayMask & (1u << w)) &&
          (metadata[w].readLane(0) & DCACHE_META_TAG_MASK) == tag)
        selected = w;
  }
  return selected;
}

uint8_t DCache::selectVictim(uint32_t set) const {
  uint8_t selected = dcachePlruVictim(plru[set]);
  bool found = false;
  for (int w = 0; w < NUM_OF_DCACHE_WAYS; ++w)
    if (!found && !validBits[set][w]) { selected = w; found = true; }
  return selected;
}

uint32_t DCache::sramRaw(int slot, int way) const {
  const auto &request = RequestBuffer[slot].request;
  const unsigned bank = (request.address & DCACHE_OFFSET_MASK) >> 2;
  uint32_t low = 0, high = 0;
  for (int lane = 0; lane < 4; ++lane) {
    low |= datas[way][bank].readLane(lane) << (lane << 3);
    if (bank + 1 < DCACHE_WORDS_PER_LINE)
      high |= datas[way][bank + 1].readLane(lane) << (lane << 3);
  }
  return extractLoadWord(low, high, request.address, request.nBytes);
}

uint32_t DCache::memoryRaw(int slot, const AXIData &dmem) const {
  const auto &request = RequestBuffer[slot].request;
  uint32_t raw = 0;
  for (int i = 0; i < 4; ++i)
    if (i < request.nBytes)
      raw |= uint32_t(dmem.reply().lineData[(request.address & DCACHE_OFFSET_MASK) + i]) << (i << 3);
  return raw;
}

void DCache::plan(DCacheInput &input) const {
  input.lookup = input.response = input.store = -1;
  input.mergeSlot = input.mergeMiss = input.allocate = input.fillMiss = input.writeAck = -1;
  input.fillBeat = input.fillFire = false;
  input.exitValid = false;
  input.exitValue = 0;
  input.loadResp = {};
  input.memoryRequest = {};
  input.InputDCacheDataSRAMInput = {};
  input.InputDCacheMetadataSRAMInput = {};
  const auto &dmem = input.DMEMModule;
  const uint8_t active = activeMask();
  const int first = oldestRequest(active);
  const int way = hitWay();
  input.readHitRaw = readValid && way >= 0 ? sramRaw(readIndex, way) : 0;
  if (first >= 0 && RequestBuffer[first].request.address == AXI_EXIT_ADDR &&
      RequestBuffer[first].request.op == Operation::Store && dmem.exitReady()) {
    assert(RequestBuffer[first].request.nBytes == 4);
    input.exitValid = true;
    input.exitValue = static_cast<uint32_t>(RequestBuffer[first].request.value);
  }

  if (dmem.isWriteReplyReady()) {
    const auto ack = dmem.writeReply();
    assert(ack.transaction < DCACHE_MSHR_CAP);
    const auto &m = misses[ack.transaction];
    assert(m.valid && m.dirty && m.writeSent && !m.writeDone &&
           victimAddress(ack.transaction) == ack.address);
    input.writeAck = ack.transaction;
  }
  if (dmem.isReplyReady()) {
    const auto &reply = dmem.reply();
    assert(reply.transaction < DCACHE_MSHR_CAP);
    const auto &m = misses[reply.transaction];
    assert(m.valid && m.readSent && m.line == reply.address);
    const bool writeComplete = !m.dirty || m.writeDone || input.writeAck == reply.transaction;
    if (writeComplete) {
      input.fillMiss = reply.transaction;
      input.fillBeat = true;
      input.fillFire = m.fillWord == DCACHE_WORDS_PER_LINE - DCACHE_FILL_WORDS_PER_BEAT;
    }
  }
  const int fillOwner = input.fillMiss >= 0 ? misses[input.fillMiss].owner : -1;
  const bool early = input.fillBeat &&
      RequestBuffer[fillOwner].request.op == Operation::Load &&
      !misses[input.fillMiss].responded;
  if (early) input.response = fillOwner;
  else {
    uint8_t ready = 0;
    for (int slot = 0; slot < REQUEST_CAP; ++slot)
      if (RequestBuffer[slot].active && RequestBuffer[slot].request.op == Operation::Load &&
          !storeHazard(slot, false) &&
          (RequestBuffer[slot].ready || (readValid && readIndex == slot && way >= 0)))
        ready |= static_cast<uint8_t>(1u << slot);
    input.response = oldestRequest(ready);
  }
  if (input.response >= 0) {
    const auto &entry = RequestBuffer[input.response];
    const uint32_t raw = early ? memoryRaw(input.response, dmem)
        : (readValid && readIndex == input.response && way >= 0)
            ? input.readHitRaw : rawPayload[input.response];
    if (!entry.cancelled && (!input.squashDetect.needSquash ||
        ROB::isOlder(entry.request.robTag, input.squashDetect.SquashTag)))
      input.loadResp = {true, entry.request.memIndex, entry.request.robTag, int32_t(raw)};
  }

  if (!input.fillBeat && first >= 0 && RequestBuffer[first].request.op == Operation::Store &&
      !setLocked(RequestBuffer[first].request.address) &&
      (RequestBuffer[first].ready || (readValid && readIndex == first && way >= 0)))
    input.store = first;

  // Classify only real synchronous lookup results. A deferred miss rechecks
  // current SRAM/tag/PLRU state before choosing a victim; it never keeps a
  // speculative victim reservation that might become stale.
  if (readValid && way < 0 && !storeHazard(readIndex, true)) {
    const uint32_t line = RequestBuffer[readIndex].request.address & ~DCACHE_OFFSET_MASK;
    if (findMiss(line) < 0 && !setLocked(line)) input.allocate = freeMiss();
  }
  if (!readValid) {
    uint8_t mergeable = 0;
    for (int slot = 0; slot < REQUEST_CAP; ++slot) {
      const auto &entry = RequestBuffer[slot];
      if (!entry.active || entry.ready || entry.miss >= 0 ||
          entry.request.op != Operation::Load || storeHazard(slot, true)) continue;
      const int id = findMiss(entry.request.address & ~DCACHE_OFFSET_MASK);
      if (id >= 0 && RequestBuffer[misses[id].owner].request.op == Operation::Load &&
          misses[id].fillWord == 0 && !misses[id].responded && input.fillMiss != id)
        mergeable |= static_cast<uint8_t>(1u << slot);
    }
    input.mergeSlot = oldestRequest(mergeable);
    if (input.mergeSlot >= 0)
      input.mergeMiss = findMiss(RequestBuffer[input.mergeSlot].request.address & ~DCACHE_OFFSET_MASK);
  }

  {
    uint8_t eligible = 0;
    bool anyMiss = false;
    for (int m = 0; m < DCACHE_MSHR_CAP; ++m) anyMiss = anyMiss || misses[m].valid;
    for (int slot = 0; slot < REQUEST_CAP; ++slot) {
      const auto &entry = RequestBuffer[slot];
      if (!entry.active || entry.ready || entry.miss >= 0 || slot == input.mergeSlot ||
          entry.request.address == AXI_EXIT_ADDR ||
          setLocked(entry.request.address) || storeHazard(slot, false)) continue;
      if (findMiss(entry.request.address & ~DCACHE_OFFSET_MASK) >= 0) continue;
      if (entry.request.op == Operation::Store && slot != first) continue;
      if (entry.deferred && (slot != first || freeMiss() < 0)) continue;
      if (!anyMiss && slot != first) continue;
      eligible |= static_cast<uint8_t>(1u << slot);
    }
    input.lookupCandidate = oldestRequest(eligible);
    if (!readValid && !input.fillBeat && input.store < 0) input.lookup = input.lookupCandidate;
  }

  uint8_t readable = 0, writable = 0;
  for (int m = 0; m < DCACHE_MSHR_CAP; ++m) {
    if (misses[m].valid) {
      if (!misses[m].readSent && !victimHazard(m) &&
          !storeHazard(misses[m].owner, true)) readable |= static_cast<uint8_t>(1u << m);
      if (misses[m].dirty && !misses[m].writeSent) writable |= static_cast<uint8_t>(1u << m);
    }
  }
  const int read = oldestMiss(readable), write = oldestMiss(writable);
  const bool writePending = write >= 0;
  if (read >= 0 && dmem.canAcceptRead(writePending)) {
    input.memoryRequest.readValid = true;
    input.memoryRequest.read.address = misses[read].line;
    input.memoryRequest.read.transaction = read;
  }
  if (write >= 0 && dmem.canAcceptWrite()) {
    input.memoryRequest.writeValid = true;
    input.memoryRequest.write.address = victimAddress(write);
    input.memoryRequest.write.transaction = write;
    for (int byte = 0; byte < DCACHE_BLOCK_CAP; ++byte)
      input.memoryRequest.write.lineData[byte] = victimPayload[write][byte >> 2] >> ((byte & 3) << 3);
  }

  auto &data = input.InputDCacheDataSRAMInput;
  auto &meta = input.InputDCacheMetadataSRAMInput;
  const uint32_t lookupSet = (RequestBuffer[unsigned(input.lookupCandidate) & (REQUEST_CAP - 1)].request.address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
  for (int w = 0; w < NUM_OF_DCACHE_WAYS; ++w) {
    meta[w].addr = lookupSet;
    for (int b = 0; b < DCACHE_WORDS_PER_LINE; ++b) data[w][b].addr = lookupSet;
  }
  if (input.fillBeat) {
    const auto &m = misses[input.fillMiss];
    const auto &request = RequestBuffer[m.owner].request;
    const uint32_t set = (m.line >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
    for (int word = 0; word < DCACHE_FILL_WORDS_PER_BEAT; ++word) {
      const int bank = m.fillWord + word;
      auto &port = data[m.way][bank];
      port.enable = port.writeEnable = true;
      port.addr = set;
      for (int lane = 0; lane < RV32_WORD_BYTES; ++lane) {
        const uint32_t byte = (bank << RV32_WORD_BYTE_BITS) + lane;
        uint32_t value = dmem.reply().lineData[byte];
        for (int s = 0; s < 4; ++s)
          if (request.op == Operation::Store && s < request.nBytes &&
              request.address + s < MEM_SIZE &&
              byte == (request.address & DCACHE_OFFSET_MASK) + uint32_t(s))
            value = uint32_t(request.value) >> (s << 3);
        port.writeData[lane] = value;
        port.writeMask[lane] = true;
      }
    }
    if (input.fillFire) {
      auto &port = meta[m.way];
      port.enable = port.writeEnable = true;
      port.addr = set;
      port.writeMask[0] = true;
      port.writeData[0] = (m.line >> DCACHE_TAG_SHIFT) |
          (request.op == Operation::Store ? DCACHE_META_DIRTY : 0u);
    }
  } else if (input.store >= 0) {
    const auto &entry = RequestBuffer[input.store];
    const auto &request = entry.request;
    const int w = readValid && readIndex == input.store && way >= 0 ? way : entry.way;
    const uint32_t set = (request.address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
    for (int byte = 0; byte < 4; ++byte) {
      if (byte < request.nBytes && request.address + byte < MEM_SIZE) {
        const uint32_t lane = (request.address & DCACHE_OFFSET_MASK) + byte;
        auto &port = data[w][lane >> RV32_WORD_BYTE_BITS];
        port.enable = port.writeEnable = true;
        port.addr = set;
        port.writeMask[lane & 3] = true;
        port.writeData[lane & 3] = uint32_t(request.value) >> (byte << 3);
      }
    }
    auto &port = meta[w];
    port.enable = port.writeEnable = true;
    port.addr = set;
    port.writeMask[0] = true;
    port.writeData[0] = (request.address >> DCACHE_TAG_SHIFT) | DCACHE_META_DIRTY;
  } else if (input.lookup >= 0) {
    const uint32_t set = (RequestBuffer[input.lookup].request.address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
    for (int w = 0; w < NUM_OF_DCACHE_WAYS; ++w) {
      meta[w].enable = true;
      meta[w].addr = set;
      for (int bank = 0; bank < DCACHE_WORDS_PER_LINE; ++bank) {
        data[w][bank].enable = true;
        data[w][bank].addr = set;
      }
    }
  }
}

void DCache::tick(const DCacheInput &input, systemState &CPUstate) {
  const uint8_t active = activeMask();
  assert(std::popcount(active) == count && count <= REQUEST_CAP);
  for (int m = 0; m < DCACHE_MSHR_CAP; ++m) if (misses[m].valid) {
    assert(RequestBuffer[misses[m].owner].active && RequestBuffer[misses[m].owner].miss == m);
    for (int n = m + 1; n < DCACHE_MSHR_CAP; ++n)
      assert(!misses[n].valid ||
          ((misses[n].line >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK) !=
          ((misses[m].line >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK));
  }
  const int fillOwner = input.fillMiss >= 0 ? misses[input.fillMiss].owner : -1;
  uint8_t released = 0;
  if (input.response >= 0 && input.response != fillOwner)
    released |= static_cast<uint8_t>(1u << input.response);
  if (input.store >= 0) released |= static_cast<uint8_t>(1u << input.store);
  if (input.fillFire) released |= static_cast<uint8_t>(1u << fillOwner);
  if (input.exitValid) released |= static_cast<uint8_t>(1u << oldestRequest(active));
  const int allocation = input.decision.valid ? freeRequest() : -1;
  assert(!input.decision.valid || (allocation >= 0 && count < REQUEST_CAP));
  const bool firstFill = input.fillBeat && misses[input.fillMiss].fillWord == 0;
  const int hit = hitWay();
  const uint32_t readSet = readValid
      ? (RequestBuffer[readIndex].request.address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK : 0;
  const uint8_t victimWay = readValid && hit < 0 ? selectVictim(readSet) : 0;

  for (int slot = 0; slot < REQUEST_CAP; ++slot) {
    if (slot == allocation) {
      const auto &request = input.decision.request;
      assert(request.nBytes == 1 || request.nBytes == 2 || request.nBytes == 4);
      assert((request.address & DCACHE_OFFSET_MASK) + request.nBytes <= DCACHE_BLOCK_CAP);
      assert(request.op == Operation::Load || request.op == Operation::Store);
      DCachePark entry{};
      entry.active = true;
      entry.request = request;
      CPUstate.DCacheModule.RequestBuffer[slot] = entry;
    } else {
      const auto &entry = RequestBuffer[slot];
      CPUstate.DCacheModule.RequestBuffer[slot].active = entry.active && !(released & (1u << slot));
      if (released & (1u << slot)) {
        CPUstate.DCacheModule.RequestBuffer[slot].ready = false;
        CPUstate.DCacheModule.RequestBuffer[slot].deferred = false;
        CPUstate.DCacheModule.RequestBuffer[slot].miss = -1;
      } else if (entry.active) {
        const bool mergedReady = firstFill &&
            RequestBuffer[fillOwner].request.op == Operation::Load &&
            entry.miss == input.fillMiss && slot != fillOwner;
        if (mergedReady) {
          CPUstate.DCacheModule.rawPayload[slot] = memoryRaw(slot, input.DMEMModule);
          CPUstate.DCacheModule.RequestBuffer[slot].ready = true;
          CPUstate.DCacheModule.RequestBuffer[slot].deferred = false;
          CPUstate.DCacheModule.RequestBuffer[slot].miss = -1;
        } else if (readValid && readIndex == slot) {
          if (hit >= 0) {
            CPUstate.DCacheModule.rawPayload[slot] = input.readHitRaw;
            CPUstate.DCacheModule.RequestBuffer[slot].ready = true;
            CPUstate.DCacheModule.RequestBuffer[slot].way = hit;
          } else {
            CPUstate.DCacheModule.RequestBuffer[slot].miss = input.allocate;
            CPUstate.DCacheModule.RequestBuffer[slot].deferred = input.allocate < 0;
          }
        }
        if (input.mergeSlot == slot) {
          CPUstate.DCacheModule.RequestBuffer[slot].miss = input.mergeMiss;
          CPUstate.DCacheModule.RequestBuffer[slot].deferred = false;
        }
      }
      if (entry.active && entry.request.op == Operation::Load && input.squashDetect.needSquash &&
          !ROB::isOlder(entry.request.robTag, input.squashDetect.SquashTag))
        CPUstate.DCacheModule.RequestBuffer[slot].cancelled = true;
    }
    uint8_t relation = 0;
    if ((active & ~released) & (1u << slot)) {
      relation = olderMask[slot] & active & ~released;
      if (allocation >= 0) relation |= static_cast<uint8_t>(1u << allocation);
    }
    CPUstate.DCacheModule.olderMask[slot] = relation;
  }

  for (int m = 0; m < DCACHE_MSHR_CAP; ++m) {
    if (input.allocate == m) {
      assert(readValid && hit < 0 && !misses[m].valid &&
             !setLocked(RequestBuffer[readIndex].request.address));
      const uint32_t meta = metadata[victimWay].readLane(0);
      const bool valid = (readWayMask & (1u << victimWay)) != 0;
      CPUstate.DCacheModule.misses[m].valid = true;
      CPUstate.DCacheModule.misses[m].line = RequestBuffer[readIndex].request.address & ~DCACHE_OFFSET_MASK;
      CPUstate.DCacheModule.misses[m].owner = readIndex;
      CPUstate.DCacheModule.misses[m].way = victimWay;
      CPUstate.DCacheModule.misses[m].victimTag = valid ? meta & DCACHE_META_TAG_MASK : 0u;
      CPUstate.DCacheModule.misses[m].dirty = valid && (meta & DCACHE_META_DIRTY) != 0;
      CPUstate.DCacheModule.misses[m].readSent = false;
      CPUstate.DCacheModule.misses[m].writeSent = false;
      CPUstate.DCacheModule.misses[m].writeDone = false;
      CPUstate.DCacheModule.misses[m].responded = false;
      CPUstate.DCacheModule.misses[m].fillWord = 0;
      for (int word = 0; word < DCACHE_WORDS_PER_LINE; ++word) {
        uint32_t value = 0;
        for (int lane = 0; lane < 4; ++lane) value |= datas[victimWay][word].readLane(lane) << (lane << 3);
        CPUstate.DCacheModule.victimPayload[m][word] = value;
      }
    } else if (input.fillFire && input.fillMiss == m) {
      CPUstate.DCacheModule.misses[m].valid = false;
    } else if (misses[m].valid) {
      if (input.memoryRequest.readValid && input.memoryRequest.read.transaction == m &&
          input.DMEMModule.canAcceptRead(input.memoryRequest.writeValid))
        CPUstate.DCacheModule.misses[m].readSent = true;
      if (input.memoryRequest.writeValid && input.memoryRequest.write.transaction == m &&
          input.DMEMModule.canAcceptWrite()) CPUstate.DCacheModule.misses[m].writeSent = true;
      if (input.writeAck == m) CPUstate.DCacheModule.misses[m].writeDone = true;
      if (input.fillMiss == m && input.fillBeat)
        CPUstate.DCacheModule.misses[m].fillWord = misses[m].fillWord + DCACHE_FILL_WORDS_PER_BEAT;
      if (input.fillMiss == m && input.response == misses[m].owner)
        CPUstate.DCacheModule.misses[m].responded = true;
    }
  }
  if (readValid) {
    if (hit >= 0) CPUstate.DCacheModule.hitCount = hitCount + 1;
    else CPUstate.DCacheModule.missCount = missCount + 1;
  }
  for (int set = 0; set < NUM_OF_DCACHE_SETS; ++set) {
    if (input.fillFire && set == int((misses[input.fillMiss].line >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK))
      CPUstate.DCacheModule.plru[set] = dcachePlruTouch(plru[set], misses[input.fillMiss].way);
    else if (readValid && hit >= 0 && set == int(readSet))
      CPUstate.DCacheModule.plru[set] = dcachePlruTouch(plru[set], hit);
    for (int w = 0; w < NUM_OF_DCACHE_WAYS; ++w) {
      if (input.allocate >= 0 && set == int(readSet) && w == victimWay)
        CPUstate.DCacheModule.validBits[set][w] = false;
      else if (input.fillFire &&
          set == int((misses[input.fillMiss].line >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK) &&
          w == misses[input.fillMiss].way)
        CPUstate.DCacheModule.validBits[set][w] = true;
    }
  }
  CPUstate.DCacheModule.readValid = input.lookup >= 0;
  if (input.lookup >= 0) {
    const uint32_t set = (RequestBuffer[input.lookup].request.address >> DCACHE_OFFSET_BITS) & DCACHE_INDEX_MASK;
    uint8_t mask = 0;
    for (int w = 0; w < NUM_OF_DCACHE_WAYS; ++w)
      if (validBits[set][w]) mask |= static_cast<uint8_t>(1u << w);
    CPUstate.DCacheModule.readIndex = input.lookup;
    CPUstate.DCacheModule.readWayMask = mask;
  }
  if (allocation >= 0) CPUstate.DCacheModule.cursor = (allocation + 1) & (REQUEST_CAP - 1);
  CPUstate.DCacheModule.count = count + unsigned(input.decision.valid) - std::popcount(released);
  for (int w = 0; w < NUM_OF_DCACHE_WAYS; ++w) {
    for (int bank = 0; bank < DCACHE_WORDS_PER_LINE; ++bank)
      datas[w][bank].tick(input.InputDCacheDataSRAMInput[w][bank], CPUstate.DCacheModule.datas[w][bank]);
    metadata[w].tick(input.InputDCacheMetadataSRAMInput[w], CPUstate.DCacheModule.metadata[w]);
  }
}
