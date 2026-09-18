#pragma once
#include "common.h"
#include "tools.h"
#include <array>
#include <cstdint>

static_assert(BRU_CAP == 4); // slot scans are fixed-length over BRU_CAP
static_assert(static_cast<uint32_t>(Operation::OP_INVALID) < 32); // op fits Wire<5>

struct BRUInput {
  Wire<1> needSquash;
  Wire<8> SquashTag;
  // dispatch payload from the DispatchArbiter's bru grant (wired with lambdas
  // that guard on dispatch.valid)
  Wire<1> dispatchValid;
  Wire<32> src1Value;
  Wire<32> src2Value;
  Wire<32> pc;
  Wire<32> imm;
  Wire<5> op;
  Wire<8> dispatchRobTag;
};

struct BRUEntry {
  Register<32> pcFrom;
  Register<32> pcResult;
  Register<8> robTag;
};

struct BRUOutput {
  std::array<BRUEntry, BRU_CAP> slots;
  std::array<Register<1>, BRU_CAP> slotValid;
};

class BRU : public dark::Module<BRUInput, BRUOutput> {
public:
  // bridge accessors over the committed (_M_old) state; signatures unchanged
  // so unconverted consumers (BPU update / FlushArbiter / CDB arbiter) keep
  // working
  bool isFull() const;
  bool isEmpty() const;
  int32_t headPCFrom() const;
  int32_t headPCResult() const;
  uint8_t headRobTag() const;
  void work() override;
};
