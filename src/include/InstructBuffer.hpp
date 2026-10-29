#pragma once
#include "common.hpp"
#include <cstdint>
#include <cstring>
struct InstructBufferEntry {
  uint32_t raw;
  int pc;
  int32_t predictedPC;
  uint8_t ckptId;
};
struct ICache;
struct DecodeUnit;
struct FQInput {
  SquashInfo squashDetect;
  const ICache &ICacheModule;
  const DecodeUnit &DecodeUnitModule;
  bool returnAccepted = false; // Same handshake as ICache.popConsume.
  bool issueValid = false; // Actual IQ dequeue, evaluated from the old IQ head.
  unsigned decodeCount = 0; // The same accepted OLD prefix consumed by IQ.
  FQInput(const ICache &icache, const DecodeUnit &du)
      : ICacheModule(icache), DecodeUnitModule(du) {}
};
struct lastPush {
  bool valid = false;
  uint32_t raw_inst;
  uint32_t pc;
};
struct systemState;
class InstructBuffer {
private:
  InstructBufferEntry InstructBufferEntries[FQ_CAP];
  std::array<lastPush, FETCH_WIDTH> pushCache{};
  uint8_t head = 0;
  uint8_t tail = 0;
  void push(uint32_t raw, int pc, int32_t predictedPC, uint8_t ckptId, systemState &CPUstate) const;
  void pop(systemState &CPUstate) const;
  void clear(systemState &CPUstate) const;

public:
  InstructBuffer() { std::memset(this, 0, sizeof(*this)); }
  bool isFull() const;
  bool isEmpty() const;
  unsigned count() const { return (tail - head) & FQ_SEQ_MASK; }
  const InstructBufferEntry &entry(unsigned lane) const {
    return InstructBufferEntries[(head + lane) & (FQ_CAP - 1)];
  }
  bool canAccept(bool willPop) const {
    return !isFull() || (willPop && !isEmpty());
  }
  bool canAccept(unsigned incoming, unsigned outgoing) const {
    return count() + incoming <= FQ_CAP + outgoing;
  }
  uint32_t headRaw() const;
  int headpc() const;
  int32_t headPredictedPC() const;
  uint8_t headCkptId() const;
  lastPush getLastPush(unsigned lane = 0) const { return pushCache[lane]; }
  void tick(const FQInput &, systemState &);
};
