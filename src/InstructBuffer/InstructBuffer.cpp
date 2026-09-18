#include "../include/InstructBuffer.hpp"
#include "../include/CPU.hpp"
#include <cstdint>
#include <stdexcept>

bool InstructBuffer::isFull() const {
  return ((tail + 1) & (FQ_CAP - 1)) == head;
}

bool InstructBuffer::isEmpty() const { return head == tail; }

void InstructBuffer::push(uint32_t raw, int pc, int32_t predictedPC,
                          uint8_t ckptId) {
  InstructBufferEntry entry{};
  entry.raw = raw;
  entry.pc = pc;
  entry.predictedPC = predictedPC;
  entry.ckptId = ckptId;
  InstructBufferEntries[tail] = entry;
  tail = (tail + 1) & (FQ_CAP - 1);
}

int32_t InstructBuffer::headPredictedPC() const {
  if (isEmpty())
    throw std::runtime_error("headPredictedPC on empty InstructBuffer!");
  return InstructBufferEntries[head].predictedPC;
}
uint8_t InstructBuffer::headCkptId() const {
  if (isEmpty())
    throw std::runtime_error("headCkptId on empty InstructBuffer!");
  return InstructBufferEntries[head].ckptId;
}

uint32_t InstructBuffer::headRaw() const {
  if (isEmpty())
    throw std::runtime_error("headRaw on empty InstructBuffer!");
  return InstructBufferEntries[head].raw;
}

int InstructBuffer::headpc() const {
  if (isEmpty())
    throw std::runtime_error("headpc on empty InstructBuffer!");
  return InstructBufferEntries[head].pc;
}

void InstructBuffer::pop() { head = (head + 1) & (FQ_CAP - 1); }

// index-based getters removed; use head* accessors for head entry

void InstructBuffer::clear() {
  std::memset(this, 0, sizeof(*this));
  head = tail = 0;
}

void InstructBuffer::tick(const FQInput &input, systemState &CPUstate) {
  CPUstate.FQModule.pushCache = {0, 0, 0};
  if (input.squashDetect.needSquash) {
    CPUstate.FQModule.clear();
    return;
  }
  if (input.ICacheModule.isReturnReady()) {
    if (!input.haltFetched && !isFull()) {
      uint32_t raw = input.ICacheModule.returnRaw();
      uint32_t pc = input.ICacheModule.returnPC();
      uint32_t predPC = input.ICacheModule.returnPredictPC();
      CPUstate.FQModule.pushCache = {true, raw, pc};
      CPUstate.FQModule.push(raw, pc, predPC,
                             input.ICacheModule.returnCkptId());
    }
  }
  if (!isEmpty() && !input.DecodeUnitModule.isFull())
    CPUstate.FQModule.pop();
}
