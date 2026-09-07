#pragma once
#include "common.h"
#include "tools.h"
#include <array>
#include <cstdint>

static_assert(ALU_CAP == 4); // slot scans are fixed-length over ALU_CAP
static_assert(static_cast<uint32_t>(Operation::OP_INVALID) < 32); // op fits Wire<5>

struct ALUInput {
  Wire<1> needSquash;
  Wire<7> SquashTag;
  Wire<1> dispatchValid;
  Wire<32> src1Value;
  Wire<32> src2Value;
  Wire<5> op;
  Wire<7> dispatchRobTag;
  Wire<1> cdbValid;
  Wire<7> cdbRobTag;
};

struct ALUEntry {
  Register<32> value;
  Register<7> robTag;
  Register<1> isControl;
};

struct ALUOutput {
  std::array<ALUEntry, ALU_CAP> slots;
  std::array<Register<1>, ALU_CAP> slotValid;
};

class ALU : public dark::Module<ALUInput, ALUOutput> {
public:
  // bridge accessors over the committed (_M_old) state; consumed by the
  // stateless arbiters' CPU-side input wiring
  bool isFull() const;
  bool isEmpty() const;
  int32_t headValue() const;
  uint8_t headRobTag() const;
  bool headIsControl() const;
  bool isValid(int index) const {
    return static_cast<bool>(slotValid[index]);
  }
  void work() override;
};
