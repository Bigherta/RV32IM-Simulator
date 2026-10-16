#include "../include/DMEM.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cassert>
#include <cstdint>

void DMEM::snapshotFrom(const DMEM &other) {
  requests = other.requests;
  head = other.head;
  count = other.count;
}

int DMEM::oldestPending(bool write) const {
  int selected = -1;
  for (int age = 0; age < DMEM_CAP; ++age) {
    const uint8_t slot = (head + age) & DMEM_MASK;
    if (selected < 0 && age < count && !requests[slot].consumed &&
        requests[slot].isWrite == write)
      selected = slot;
  }
  return selected;
}

bool DMEM::isReplyReady() const {
  const int slot = oldestPending(false);
  return slot >= 0 && requests[slot].remainCycle == 0;
}

bool DMEM::isWriteReplyReady() const {
  const int slot = oldestPending(true);
  return slot >= 0 && requests[slot].remainCycle == 0;
}

const MemReply &DMEM::reply() const {
  const int slot = oldestPending(false);
  assert(slot >= 0);
  return requests[slot].payload;
}

MemWriteReply DMEM::writeReply() const {
  const int slot = oldestPending(true);
  assert(slot >= 0);
  return {requests[slot].payload.address, requests[slot].payload.transaction};
}

void DMEM::writeLine(const DMEMEntry &request, systemState &CPUstate) const {
  for (int i = 0; i < DCACHE_BLOCK_CAP; i++) {
    const uint32_t address = request.payload.address + i;
    if (address < MEM_SIZE)
      CPUstate.DMEMModule.mem[address] = request.payload.lineData[i];
  }
}

DMEMReadCompletion DMEM::sampleReadCompletion() const {
  DMEMReadCompletion completion{};
  // One read acceptance per cycle and a fixed execution latency imply
  // at most one newly completing read in each clock, even with held replies.
  for (int age = 0; age < DMEM_CAP; ++age) {
    const uint8_t slot = (head + age) & DMEM_MASK;
    if (age < count && !requests[slot].isWrite && requests[slot].remainCycle == 1) {
      assert(!completion.valid);
      completion.valid = true;
      completion.slot = slot;
      const auto base = requests[slot].payload.address;
      for (int i = 0; i < DCACHE_BLOCK_CAP; ++i)
        completion.line[i] = read_data(base + i);
    }
  }
  return completion;
}

void DMEM::tick(const DMEMInput &input, systemState &CPUstate) {
  assert(count <= DMEM_CAP);
  const bool writePush = input.request.writeValid && canAcceptWrite();
  const bool readPush = input.request.readValid && canAcceptRead(input.request.writeValid);
  const bool readPop = input.replyConsumed && isReplyReady();
  const bool writePop = input.writeConsumed && isWriteReplyReady();
  const int readSlot = readPop ? oldestPending(false) : -1;
  const int writeSlot = writePop ? oldestPending(true) : -1;
  assert(!input.replyConsumed || isReplyReady());
  assert(!input.writeConsumed || isWriteReplyReady());
  // Both ports share old free space. A simultaneous write is appended first;
  // the read takes the next slot, only when a second old credit exists.
  if (readPush) {
    const auto slot = (head + count + unsigned(writePush)) & DMEM_MASK;
    assert((input.request.read.address & DCACHE_OFFSET_MASK) == 0);
    CPUstate.DMEMModule.requests[slot].payload.address = input.request.read.address;
    CPUstate.DMEMModule.requests[slot].payload.transaction = input.request.read.transaction;
    CPUstate.DMEMModule.requests[slot].remainCycle = MEM_LATENCY;
    CPUstate.DMEMModule.requests[slot].isWrite = false;
    CPUstate.DMEMModule.requests[slot].consumed = false;
  }
  if (writePush) {
    const auto slot = (head + count) & DMEM_MASK;
    assert((input.request.write.address & DCACHE_OFFSET_MASK) == 0);
    CPUstate.DMEMModule.requests[slot].payload.address = input.request.write.address;
    CPUstate.DMEMModule.requests[slot].payload.transaction = input.request.write.transaction;
    CPUstate.DMEMModule.requests[slot].remainCycle = MEM_LATENCY;
    CPUstate.DMEMModule.requests[slot].isWrite = true;
    CPUstate.DMEMModule.requests[slot].consumed = false;
    for (int byte = 0; byte < DCACHE_BLOCK_CAP; ++byte)
      CPUstate.DMEMModule.requests[slot].payload.lineData[byte] = input.request.write.lineData[byte];
  }
  for (int age = 0; age < DMEM_CAP; ++age) {
    const uint8_t slot = (head + age) & DMEM_MASK;
    if (age < count) {
      const auto &request = requests[slot];
      assert(!request.consumed || request.remainCycle == 0);
      if (request.remainCycle > 0) {
        CPUstate.DMEMModule.requests[slot].remainCycle = request.remainCycle - 1;
        if (request.remainCycle == 1) {
          if (request.isWrite) {
            writeLine(request, CPUstate);
          } else {
            assert(input.completedRead.valid && input.completedRead.slot == slot);
            for (int byte = 0; byte < DCACHE_BLOCK_CAP; ++byte)
              CPUstate.DMEMModule.requests[slot].payload.lineData[byte] = input.completedRead.line[byte];
          }
        }
      }
      if (slot == readSlot || slot == writeSlot)
        CPUstate.DMEMModule.requests[slot].consumed = true;
    }
  }
  uint8_t reclaimed = 0;
  bool blocked = false;
  for (int age = 0; age < DMEM_CAP; ++age) {
    const uint8_t slot = (head + age) & DMEM_MASK;
    if (age < count && !blocked) {
      if (requests[slot].consumed || slot == readSlot || slot == writeSlot)
        ++reclaimed;
      else
        blocked = true;
    }
  }
  CPUstate.DMEMModule.head = (head + reclaimed) & DMEM_MASK;
  CPUstate.DMEMModule.count = count + unsigned(writePush) + unsigned(readPush) - reclaimed;
}
