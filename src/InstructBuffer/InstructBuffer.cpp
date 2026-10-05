#include "../include/InstructBuffer.hpp"
#include "../include/CPU.hpp"
#include <cstdint>
#include <stdexcept>

bool InstructBuffer::isFull() const {
  return tail == (head ^ FQ_CAP);
}

bool InstructBuffer::isEmpty() const { return head == tail; }

void InstructBuffer::push(uint32_t raw, int pc, int32_t predictedPC,
                          uint8_t ckptId, systemState &CPUstate) const {
  InstructBufferEntry entry{};
  entry.raw = raw;
  entry.pc = pc;
  entry.predictedPC = predictedPC;
  entry.ckptId = ckptId;
  CPUstate.FQModule.InstructBufferEntries[tail & (FQ_CAP - 1)] = entry;
  CPUstate.FQModule.tail = (tail + 1) & FQ_SEQ_MASK;
}

int32_t InstructBuffer::headPredictedPC() const {
  if (isEmpty())
    throw std::runtime_error("headPredictedPC on empty InstructBuffer!");
  return InstructBufferEntries[head & (FQ_CAP - 1)].predictedPC;
}
uint8_t InstructBuffer::headCkptId() const {
  if (isEmpty())
    throw std::runtime_error("headCkptId on empty InstructBuffer!");
  return InstructBufferEntries[head & (FQ_CAP - 1)].ckptId;
}

uint32_t InstructBuffer::headRaw() const {
  if (isEmpty())
    throw std::runtime_error("headRaw on empty InstructBuffer!");
  return InstructBufferEntries[head & (FQ_CAP - 1)].raw;
}

int InstructBuffer::headpc() const {
  if (isEmpty())
    throw std::runtime_error("headpc on empty InstructBuffer!");
  return InstructBufferEntries[head & (FQ_CAP - 1)].pc;
}

void InstructBuffer::pop(systemState &CPUstate) const {
  CPUstate.FQModule.head = (head + 1) & FQ_SEQ_MASK;
}

// index-based getters removed; use head* accessors for head entry

void InstructBuffer::clear(systemState &CPUstate) const {
  CPUstate.FQModule.head = 0;
  CPUstate.FQModule.tail = 0;
  CPUstate.FQModule.pushCache = {};
}

void InstructBuffer::tick(const FQInput &input, systemState &CPUstate) {
  CPUstate.FQModule.pushCache = {0, 0, 0};
  if (input.squashDetect.needSquash) {
    clear(CPUstate);
    return;
  }
  if (input.ICacheModule.isReturnReady()) {
    if (!input.haltFetched && !isFull()) {
      uint32_t raw = input.ICacheModule.returnRaw();
      uint32_t pc = input.ICacheModule.returnPC();
      uint32_t predPC = input.ICacheModule.returnPredictPC();
      CPUstate.FQModule.pushCache = {true, raw, pc};
      push(raw, pc, predPC, input.ICacheModule.returnCkptId(), CPUstate);
    }
  }
  if (!isEmpty() && input.DecodeUnitModule.canAccept(input.issueValid))
    pop(CPUstate);
}
