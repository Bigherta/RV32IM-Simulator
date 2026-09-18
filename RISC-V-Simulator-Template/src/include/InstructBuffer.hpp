#pragma once
#include "common.h"
#include "tools.h"
#include <array>
#include <cstdint>

static_assert(FQ_CAP == 8);    // head/tail are 3-bit ring pointers
static_assert(CKPT_CAP == 64); // ckptId is a 6-bit checkpoint index

struct InstructBufferEntry {
  Register<32> raw;
  Register<32> pc;
  Register<32> predictedPC;
  Register<6> ckptId;
};

struct FQInput {
  Wire<1> needSquash;
  Wire<1> haltFetched;
  Wire<1> icacheReturnReady;
  Wire<32> icacheReturnRaw;
  Wire<32> icacheReturnPC;
  Wire<32> icacheReturnPredictedPC;
  Wire<6> icacheReturnCkptId;
  Wire<1> decodeFull;
};

struct FQOutput {
  Register<3> head;
  Register<3> tail;
  // Pre-decode observation of the last push (committed next cycle): feeds
  // the BPU's scanner for RAS maintenance / early BTB training.
  Register<1> lastValid;
  Register<32> lastRaw;
  Register<32> lastPC;
};

struct FQInner {
  std::array<InstructBufferEntry, FQ_CAP> entries;
};

struct InstructBuffer : dark::Module<FQInput, FQOutput, FQInner> {
  void work() override;

  // bridge accessors: combinational reads of the committed (_M_old) state
  // (renamed from FetchQueue; member aliases FQModule/FQ* kept for callers)
  bool isFull() const {
    return ((static_cast<uint32_t>(tail) + 1) & (FQ_CAP - 1)) ==
           static_cast<uint32_t>(head);
  }
  bool isEmpty() const {
    return static_cast<uint32_t>(head) == static_cast<uint32_t>(tail);
  }
  uint32_t getLastRaw() const {
    return static_cast<uint32_t>(lastRaw);
  }
  uint32_t getLastPC() const {
    return static_cast<uint32_t>(lastPC);
  }
  bool getLastValid() const {
    return static_cast<bool>(lastValid);
  }
  uint32_t headRaw() const {
    return static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].raw);
  }
  int headpc() const {
    return static_cast<int>(
        static_cast<uint32_t>(entries[static_cast<uint32_t>(head)].pc));
  }
  int32_t headPredictedPC() const {
    return static_cast<int32_t>(static_cast<uint32_t>(
        entries[static_cast<uint32_t>(head)].predictedPC));
  }
  uint8_t headCkptId() const {
    return static_cast<uint8_t>(static_cast<uint32_t>(
        entries[static_cast<uint32_t>(head)].ckptId));
  }
};
