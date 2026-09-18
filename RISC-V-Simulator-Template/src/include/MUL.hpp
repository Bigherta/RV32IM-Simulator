#pragma once
#include "common.h"
#include <array>
#include <cstdint>
struct MulInput {
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
struct MulEntry {
  Register<32> value;
  Register<7> robTag;
};
struct MulOutput {
  std::array<MulEntry, MUL_CAP> slots;
  std::array<Register<1>, MUL_CAP> slotValid;
};
struct Row64 {
  Register<32> lo, hi;
};
struct MulInner {
  struct PartialProductResult {
    std::array<Row64, 19> rows;
    Register<5> op;
    Register<7> robTag;
    Register<1> valid;
  } partialRes;
  struct SCResult {
    Register<32> S_lo, S_hi, C_lo, C_hi;
    Register<5> op;
    Register<7> robTag;
    Register<1> valid;
  } scRes;
};
struct MUL : dark::Module<MulInput, MulOutput, MulInner> {
  bool isFull() const;
  bool isEmpty() const;
  int32_t headValue() const;
  uint8_t headRobTag() const;
  void work() override;
};
