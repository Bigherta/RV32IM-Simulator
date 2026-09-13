#include "../include/DIV.hpp"
#include "../include/CPU.hpp"
#include <cstdint>
namespace {
uint8_t clz(uint32_t num) {
  if (num == 0)
    return 32;
  uint8_t leadingZeros = 0;
  if (num >> 16)
    num >>= 16;
  else
    leadingZeros |= 16;
  if (num >> 8)
    num >>= 8;
  else
    leadingZeros |= 8;
  if (num >> 4)
    num >>= 4;
  else
    leadingZeros |= 4;
  if (num >> 2)
    num >>= 2;
  else
    leadingZeros |= 2;
  if (num >> 1)
    num >>= 1;
  else
    leadingZeros |= 1;
  return leadingZeros;
} // require num >= 0
} // namespace
void DIV::receive(int32_t op1, int32_t op2, RobTag tag, Operation op) {
  // Stage 0 front-end (a): special cases (RISC-V semantics, return at once).
  // Parameters are (quotient, remain); the field this op does not produce is
  // written as 0. The signed paths' signs are already folded into the
  // patterns passed here (sec 2.1(b)), so no further sign fixup is needed.
  auto finish = [&](uint32_t quotientValue, uint32_t remainValue) {
    quotient = quotientValue;
    remain = remainValue;
    // The callers already fold the sign into the 32-bit pattern (sec 2.1(b)),
    // so getValue() must NOT negate again. These two flags are stale leftovers
    // from the previous instruction at this point -- clear them explicitly.
    isResultNegative = false;
    isDividendNegative = false;
    resultValid = true;
    busy = false;
    robTag = tag;
    operationType = op;
  };
  // d = 0 (checked first, so 0/0 lands here): div -> -1, divu -> 2^32-1,
  // rem/remu -> x.
  if (op == Operation::DIV && op2 == 0)
    return finish(0xFFFFFFFFu, 0u);
  if (op == Operation::DIVU && op2 == 0)
    return finish(0xFFFFFFFFu, 0u);
  if ((op == Operation::REM || op == Operation::REMU) && op2 == 0)
    return finish(0u, static_cast<uint32_t>(op1));
  // INT_MIN / -1 never traps (signed only; unsigned takes x < d).
  if (op == Operation::DIV && op1 == INT32_MIN && op2 == -1)
    return finish(0x80000000u, 0u);
  if (op == Operation::REM && op1 == INT32_MIN && op2 == -1)
    return finish(0u, 0u);
  // x < d gives quot 0 and rem x; x == d gives quot 1 and rem 0.
  // Unsigned compares patterns, signed compares magnitudes (truncate to zero).
  if (op == Operation::DIVU || op == Operation::REMU) {
    uint32_t xBits = static_cast<uint32_t>(op1);
    uint32_t dBits = static_cast<uint32_t>(op2);
    if (xBits < dBits) { // x < d
      uint32_t remValue = (op == Operation::DIVU) ? 0u : xBits;
      return finish(0u, remValue);
    }
    if (xBits == dBits) // x == d
      return finish(op == Operation::DIVU ? 1u : 0u, 0u);
  }
  if (op == Operation::DIV || op == Operation::REM) {
    uint32_t xBits = static_cast<uint32_t>(op1);
    uint32_t dBits = static_cast<uint32_t>(op2);
    uint32_t absX =
        (op1 < 0) ? (~xBits + 1u) : xBits; // |x|, exact even for INT_MIN
    uint32_t absD = (op2 < 0) ? (~dBits + 1u) : dBits; // |d|
    if (absX < absD) { // |x| < |d|: quot 0, rem x (pattern keeps its sign)
      uint32_t remValue =
          (op == Operation::DIV) ? 0u : static_cast<uint32_t>(op1);
      return finish(0u, remValue);
    }
    if (absX == absD) { // |x| == |d|: quot +-1 (xs ^ ds), rem 0
      if (op == Operation::DIV)
        return finish((op1 < 0) == (op2 < 0) ? 1u : 0xFFFFFFFFu, 0u);
      return finish(0u, 0u);
    }
  }
  resultValid = false;
  busy = true;
  robTag = tag;
  operationType = op;
  uint32_t xBits = static_cast<uint32_t>(op1);
  uint32_t dBits = static_cast<uint32_t>(op2);
  // signed magnitudes only for DIV/REM; DIVU/REMU keep the raw bit patterns
  const bool signedOp = (op == Operation::DIV || op == Operation::REM);
  isDividendNegative = signedOp && (op1 < 0);
  unsignedDividend = isDividendNegative ? (~xBits + 1u) : xBits;
  isDivisorNegative = signedOp && (op2 < 0);
  unsignedDivisor = isDivisorNegative ? (~dBits + 1u) : dBits;
  isResultNegative = (isDivisorNegative ^ isDividendNegative) ? 1 : 0;
  prepareValid = true;
} // pay attention: the caller of this function is CPUstate.DIVModule,
  // therefore, you should throw an error in tick when the current DIVModule is
  // busy
void DIV::prepare(uint64_t divisor, uint64_t dividend) {
  // unsignedDividend holds |x| (P side); unsignedDivisor holds |d| (D side).
  clzX = clz(unsignedDividend); // clzX = CLZ of |x|
  clzD = clz(unsignedDivisor);  // clzD = CLZ of |d|
  prepareValid = false;
  // the special-case judge already handled the clzX > clzD scenario
  unsignedDivisor = unsignedDivisor << clzD;   // D = |d| << clzD
  unsignedDividend = unsignedDividend << clzX; // P_1 = |x| << clzX
  auto align = clzD - clzX;
  // ceil(align/2): prepare already consumed q_1, so loop() runs k-1 ticks
  loopTimes = (align + 1) >> 1;
  // the 2^-shiftD factor lands on the divisor (sec 2.1(f))
  shiftD = align & 1;
  unsignedDivisor <<= shiftD; // D_dp = D << shiftD
  dSlice = shiftD ? (unsignedDivisor >> ulpExpWithShiftD) & 31
                  : (unsignedDivisor >> ulpExpNoShiftD) & 31;
  dSlice3 = dSlice + (dSlice << 1);
  int32_t slice =
      static_cast<int32_t>(shiftD ? (unsignedDividend >> (ulpExpWithShiftD - 1))
                                  : (unsignedDividend >> (ulpExpNoShiftD - 1)));

  if (slice >= dSlice3) {                   // q_1 = 2
    auto subtrahend = unsignedDivisor << 1; // q_1 * D_dp
    regS = (unsignedDividend ^ ~subtrahend ^ 1) << 2;
    regC = ((unsignedDividend & ~subtrahend) | (unsignedDividend & 1) |
            (~subtrahend & 1))
           << 3;
    regA = 2;
    regB = 1;
  } else if (slice >= dSlice) {        // q_1 = 1
    auto subtrahend = unsignedDivisor; // q_1 * D_dp
    regS = (unsignedDividend ^ ~subtrahend ^ 1) << 2;
    regC = ((unsignedDividend & ~subtrahend) | (unsignedDividend & 1) |
            (~subtrahend & 1))
           << 3;
    regA = 1;
    regB = 0;
  } else { // q_1 = 0: no subtrahend
    regS = unsignedDividend << 2;
    regC = 0;
    regA = 0;
    regB = 3;
  }
  loopValid = (loopTimes != 0);
  fullAdderValid = (loopTimes == 0);
}
void DIV::loop(uint64_t oldRegS, uint64_t oldRegC, uint32_t oldRegA,
               uint32_t oldRegB) {
  // QDS: two 9-bit slices -> 9-bit two's complement -> drop LSB (= estPShift)
  uint32_t sum9 = shiftD ? ((oldRegS >> sliceShiftWithShiftD) & 0x1FFu) +
                               ((oldRegC >> sliceShiftWithShiftD) & 0x1FFu)
                         : ((oldRegS >> sliceShiftNoShiftD) & 0x1FFu) +
                               ((oldRegC >> sliceShiftNoShiftD) & 0x1FFu);
  int32_t slice = (int32_t)(((sum9 & 0x1FFu) ^ 0x100u) - 0x100u) & ~1;
  // registers hold P = 4W (PW = 35 + shiftD): shift first, then 3:2 compress
  const uint64_t mask = shiftD ? (1ull << 36) - 1 : (1ull << 35) - 1;
  const uint64_t S4 = (oldRegS << 2) & mask;
  const uint64_t C4 = (oldRegC << 2) & mask;
  if (slice >= dSlice3) {                       // q = +2
    uint64_t subtrahend = unsignedDivisor << 3; // |q| * (D_dp << 2)
    uint64_t T = mask ^ subtrahend;             // ~qd; carry-in via regC bit0
    regS = (S4 ^ C4 ^ T) & mask;
    regC = ((((S4 & C4) | (S4 & T) | (C4 & T)) << 1) | 1) & mask;
    regA = (oldRegA << 2) | 2;
    regB = (oldRegA << 2) | 1;
  } else if (slice >= dSlice) { // q = +1
    uint64_t subtrahend = unsignedDivisor << 2;
    uint64_t T = mask ^ subtrahend;
    regS = (S4 ^ C4 ^ T) & mask;
    regC = ((((S4 & C4) | (S4 & T) | (C4 & T)) << 1) | 1) & mask;
    regA = (oldRegA << 2) | 1;
    regB = (oldRegA << 2) | 0;
  } else if (slice >= -dSlice) { // q = 0: no subtrahend
    regS = (S4 ^ C4) & mask;
    regC = ((S4 & C4) << 1) & mask;
    regA = oldRegA << 2;
    regB = (oldRegB << 2) | 3;
  } else if (slice >= -dSlice3) { // q = -1
    uint64_t subtrahend = unsignedDivisor << 2;
    regS = (S4 ^ C4 ^ subtrahend) & mask;
    regC = (((S4 & C4) | (S4 & subtrahend) | (C4 & subtrahend)) << 1) & mask;
    regA = (oldRegB << 2) | 3;
    regB = (oldRegB << 2) | 2;
  } else { // q = -2
    uint64_t subtrahend = unsignedDivisor << 3;
    regS = (S4 ^ C4 ^ subtrahend) & mask;
    regC = (((S4 & C4) | (S4 & subtrahend) | (C4 & subtrahend)) << 1) & mask;
    regA = (oldRegB << 2) | 2;
    regB = (oldRegB << 2) | 1;
  }
  if (--loopTimes) {
    loopValid = true;
  } else {
    loopValid = false;
    fullAdderValid = true;
  }
}
void DIV::calculateResult(uint64_t oldRegS, uint64_t oldRegC, uint32_t oldRegA,
                          uint32_t oldRegB) {
  uint64_t Pk = oldRegS + oldRegC;
  Pk &= shiftD ? (1ull << 36) - 1 : (1ull << 35) - 1;
  if ((shiftD && (Pk >> 35) & 1) || (!shiftD && (Pk >> 34) & 1)) {
    Pk -= shiftD ? 1ull << 36 : 1ull << 35;
  }
  if ((Pk >> 63) == 0) {
    quotient = oldRegA;
    remain = ((Pk >> 2) >> shiftD) >> clzD;
  } else {
    quotient = oldRegB;
    remain = (((Pk + (unsignedDivisor << 2)) >> 2) >> shiftD) >> clzD;
  }
  fullAdderValid = false;
  busy = false;
  resultValid = true;
}
void DIV::flush(uint8_t tag) {
  if (ROB::isOlder(tag, robTag)) {
    unsignedDivisor = 0;
    unsignedDividend = 0;
    prepareValid = 0;
    isDivisorNegative = 0;
    isDividendNegative = 0;
    isResultNegative = 0;
    clzX = 0;
    clzD = 0;
    loopTimes = 0;
    regS = 0;
    regC = 0;
    regA = 0;
    regB = 0;
    dSlice = 0;
    dSlice3 = 0;
    loopValid = 0;
    quotient = 0;
    remain = 0;
    robTag = 0;
    fullAdderValid = 0;
    shiftD = 0;
    resultValid = 0;
    busy = 0;
  }
}
void DIV::tick(const DIVInput &input, systemState &CPUstate) {
  auto &div = CPUstate.DIVModule;
  if (input.cdbOutput.valid) {
    div.resultValid = false;
  } // consume the result by cdb
  if (fullAdderValid) {
    div.calculateResult(regS, regC, regA, regB);
  }
  if (loopValid) {
    div.loop(regS, regC, regA, regB);
  }
  if (prepareValid) {
    div.prepare(unsignedDivisor, unsignedDividend);
  }
  if (input.dispatch.valid) {
    const auto &rs = input.RSModule.divideRS[input.dispatch.rsIndex];
    div.receive(input.PRFModule.getOperandValue(rs.src1),
                input.PRFModule.getOperandValue(rs.src2), input.dispatch.robTag,
                rs.op);
  }
  if (input.squashDetect.needSquash) {
    div.flush(input.squashDetect.SquashTag);
  }
}