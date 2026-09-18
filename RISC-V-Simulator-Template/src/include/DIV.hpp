#pragma once
#include "common.h"
#include <cstdint>
constexpr int ulpExpWithShiftD = 28;
constexpr int ulpExpNoShiftD = 27;
constexpr int sliceShiftWithShiftD = 27;
constexpr int sliceShiftNoShiftD = 26;
struct DIVInput {
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
struct DIVOutput {
  Register<1> resultValid;
};
struct DIVInner {
  Register<1> shiftD;
  Register<1> fullAdderValid;
  Register<1> loopValid;
  Register<1> prepareValid;
  Register<5> operationType;
  Register<7> robTag;
  Register<32> remain;
  Register<32> quotient;
  Register<5> dSlice;  // divisor estimate slice, <= 31
  Register<7> dSlice3; // 3*dSlice, <= 93
  Register<32> regA;   // positive-digit quotient accumulator (prefix of the
                       // quotient, always < 2^32)
  Register<32> regB;   // negative-remainder quotient accumulator
  // Carry-save P domain is PW = 35 + shiftD <= 36 bits. kMaxLength caps a
  // Register at 32, so P rides lo/hi pairs (MUL Row64 precedent); join36() in
  // DIV.cpp re-joins. Only the low 36 bits are ever consumed, so truncating
  // the store at 36 bits is lossless vs the reference's uint64_t state.
  Register<32> regSLo;
  Register<4> regSHi;
  Register<32> regCLo;
  Register<4> regCHi;
  Register<5> loopTimes;
  Register<5> clzD; // <= 31: receive()'s special cases intercept d == 0
  Register<5> clzX; // <= 31: the general path requires |x| >= |d| >= 1
  Register<1> isDividendNegative;
  Register<1> isResultNegative;
  // D_dp = |d| << clzD << shiftD < 2^33 -> lo/hi pair (join33() re-joins).
  Register<32> unsignedDivisorLo;
  Register<1> unsignedDivisorHi;
  Register<32> unsignedDividend; // |x|, raw at receive / <<clzX after prepare
};
struct DIV : dark::Module<DIVInput, DIVOutput, DIVInner> {
  void receive(int32_t op1, int32_t op2, RobTag tag, Operation op);
  void prepare();
  void loop();
  void calculateResult();
  // Unconditional zeroing; the isOlder gate lives in work() (effective-tag
  // semantics: a same-cycle dispatch replaces the tag the squash test sees).
  void flush();
  void work() override;
  bool isReady() const { return static_cast<bool>(resultValid); }
  bool canAccept() const {
    return !static_cast<bool>(prepareValid) && !static_cast<bool>(loopValid) &&
           !static_cast<bool>(fullAdderValid) &&
           !static_cast<bool>(resultValid);
  }
  uint8_t getResultRobtag() const { return static_cast<uint32_t>(robTag); }
  int32_t getValue() const {
    if (static_cast<uint32_t>(operationType) == static_cast<uint32_t>(Operation::DIV)) {
      if (static_cast<bool>(isResultNegative)) {
        return -static_cast<int32_t>(static_cast<uint32_t>(quotient));
      } else {
        return static_cast<int32_t>(static_cast<uint32_t>(quotient));
      }
    }
    if (static_cast<uint32_t>(operationType) == static_cast<uint32_t>(Operation::DIVU)) {
      return static_cast<int32_t>(static_cast<uint32_t>(quotient));
    }
    if (static_cast<uint32_t>(operationType) == static_cast<uint32_t>(Operation::REM)) {
      if (static_cast<bool>(isDividendNegative)) {
        return -static_cast<int32_t>(static_cast<uint32_t>(remain));
      } else {
        return static_cast<int32_t>(static_cast<uint32_t>(remain));
      }
    }
    if (static_cast<uint32_t>(operationType) == static_cast<uint32_t>(Operation::REMU)) {
      return static_cast<int32_t>(static_cast<uint32_t>(remain));
    }
    throw std::runtime_error("not DIV operation!");
  }
};
