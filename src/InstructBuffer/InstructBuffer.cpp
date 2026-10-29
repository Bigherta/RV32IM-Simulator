#include "../include/InstructBuffer.hpp"
#include "../include/CPU.hpp"
#include <cassert>
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
  CPUstate.FQModule.pushCache = {};
  if (input.squashDetect.needSquash) {
    clear(CPUstate);
    return;
  }
  if (input.returnAccepted) {
    assert(input.ICacheModule.isReturnReady());
    uint32_t pc = input.ICacheModule.returnPC();
    uint32_t predPC = input.ICacheModule.returnPredictPC();
    const unsigned lanes = input.ICacheModule.returnCount();
    const auto cid = input.ICacheModule.returnCkptId();
    assert(canAccept(lanes, input.decodeCount));
    for (unsigned lane = 0; lane < FETCH_WIDTH; ++lane) {
      if (lane < lanes) {
        const uint32_t lanePC = pc + (lane << 2);
        const auto raw = input.ICacheModule.returnRaw(lane);
        CPUstate.FQModule.InstructBufferEntries[(tail + lane) & (FQ_CAP - 1)] =
            {raw, int(lanePC), int32_t(lane + 1 < lanes ? lanePC + 4u : predPC), uint8_t((cid + lane) & (CKPT_CAP - 1))};
        CPUstate.FQModule.pushCache[lane] = {true, raw, lanePC};
      }
    }
    CPUstate.FQModule.tail = (tail + lanes) & FQ_SEQ_MASK;
  }
  assert(input.decodeCount <= count() && input.decodeCount <= DECODE_WIDTH);
  if (input.decodeCount != 0)
    CPUstate.FQModule.head = (head + input.decodeCount) & FQ_SEQ_MASK;
}
