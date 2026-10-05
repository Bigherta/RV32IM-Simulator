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
void DIV::receive(uint32_t op1, uint32_t op2, RobTag tag, Operation op, systemState &CPUstate) const {
  // Stage 0 front-end (a): special cases (RISC-V semantics, return at once).
  // Parameters are (quotient, remain); the field this op does not produce is
  // written as 0. The signed paths' signs are already folded into the
  // patterns passed here (sec 2.1(b)), so no further sign fixup is needed.
  auto finish = [&](uint32_t quotientValue, uint32_t remainValue) {
    CPUstate.DIVModule.quotient = quotientValue;
    CPUstate.DIVModule.remain = remainValue;
    // The callers already fold the sign into the 32-bit pattern (sec 2.1(b)),
    // so getValue() must NOT negate again. These two flags are stale leftovers
    // from the previous instruction at this point -- clear them explicitly.
    CPUstate.DIVModule.isResultNegative = false;
    CPUstate.DIVModule.isDividendNegative = false;
    CPUstate.DIVModule.resultValid = true;
    CPUstate.DIVModule.robTag = tag;
    CPUstate.DIVModule.operationType = op;
  };
  // d = 0 (checked first, so 0/0 lands here): div -> -1, divu -> 2^32-1,
  // rem/remu -> x.
  if (op == Operation::DIV && op2 == 0)
    return finish(0xFFFFFFFFu, 0u);
  if (op == Operation::DIVU && op2 == 0)
    return finish(0xFFFFFFFFu, 0u);
  if ((op == Operation::REM || op == Operation::REMU) && op2 == 0)
    return finish(0u, op1);
  // INT_MIN / -1 never traps (signed only; unsigned takes x < d).
  if (op == Operation::DIV && op1 == 0x80000000u && op2 == 0xFFFFFFFFu)
    return finish(0x80000000u, 0u);
  if (op == Operation::REM && op1 == 0x80000000u && op2 == 0xFFFFFFFFu)
    return finish(0u, 0u);
  // x < d gives quot 0 and rem x; x == d gives quot 1 and rem 0.
  // Unsigned compares patterns, signed compares magnitudes (truncate to zero).
  if (op == Operation::DIVU || op == Operation::REMU) {
    if (op1 < op2) { // x < d
      uint32_t remValue = (op == Operation::DIVU) ? 0u : op1;
      return finish(0u, remValue);
    }
    if (op1 == op2) // x == d
      return finish(op == Operation::DIVU ? 1u : 0u, 0u);
  }
  if (op == Operation::DIV || op == Operation::REM) {
    // Sign bit tests on the bit vector (no signed compare, no UB).
    const uint32_t absX =
        ((op1 >> 31) & 1u) ? (~op1 + 1u) : op1; // |x|, exact for INT_MIN
    const uint32_t absD =
        ((op2 >> 31) & 1u) ? (~op2 + 1u) : op2; // |d|
    if (absX < absD) { // |x| < |d|: quot 0, rem x (pattern keeps its sign)
      uint32_t remValue = (op == Operation::DIV) ? 0u : op1;
      return finish(0u, remValue);
    }
    if (absX == absD) { // |x| == |d|: quot +-1 (xs ^ ds), rem 0
      if (op == Operation::DIV)
        return finish((((op1 ^ op2) >> 31) & 1u) ? 0xFFFFFFFFu : 1u, 0u);
      return finish(0u, 0u);
    }
  }
  CPUstate.DIVModule.resultValid = false;
  CPUstate.DIVModule.robTag = tag;
  CPUstate.DIVModule.operationType = op;
  // signed magnitudes only for DIV/REM; DIVU/REMU keep the raw bit patterns
  const bool signedOp = (op == Operation::DIV || op == Operation::REM);
  const bool dividendNegative = signedOp && (((op1 >> 31) & 1u) != 0);
  CPUstate.DIVModule.isDividendNegative = dividendNegative;
  CPUstate.DIVModule.unsignedDividend = dividendNegative ? (~op1 + 1u) : op1;
  const bool divisorNegative = signedOp && (((op2 >> 31) & 1u) != 0);
  CPUstate.DIVModule.unsignedDivisor = divisorNegative ? (~op2 + 1u) : op2;
  CPUstate.DIVModule.isResultNegative = (divisorNegative ^ dividendNegative) ? 1 : 0;
  CPUstate.DIVModule.prepareValid = true;
} // Dispatch is admitted only when canAccept() sees all stage flags and
  // resultValid low.
void DIV::prepare(systemState &CPUstate) const {
  // unsignedDividend holds |x| (P side); unsignedDivisor holds |d| (D side).
  const auto clzXValue = clz(unsignedDividend);
  const auto clzDValue = clz(unsignedDivisor);
  CPUstate.DIVModule.clzX = clzXValue;
  CPUstate.DIVModule.clzD = clzDValue;
  CPUstate.DIVModule.prepareValid = false;
  // the special-case judge already handled the clzX > clzD scenario
  const auto dividend = unsignedDividend << clzXValue;
  auto align = clzDValue - clzXValue;
  // ceil(align/2): prepare already consumed q_1, so loop() runs k-1 ticks
  const auto loops = (align + 1) >> 1;
  CPUstate.DIVModule.loopTimes = loops;
  // the 2^-shiftD factor lands on the divisor (sec 2.1(f))
  const bool shift = align & 1;
  const auto divisor = unsignedDivisor << clzDValue << shift;
  const int32_t divisorSlice = shift ? (divisor >> ulpExpWithShiftD) & 31
                                    : (divisor >> ulpExpNoShiftD) & 31;
  const int32_t divisorSlice3 = divisorSlice + (divisorSlice << 1);
  CPUstate.DIVModule.shiftD = shift;
  CPUstate.DIVModule.unsignedDivisor = divisor;
  CPUstate.DIVModule.unsignedDividend = dividend;
  CPUstate.DIVModule.dSlice = divisorSlice;
  CPUstate.DIVModule.dSlice3 = divisorSlice3;
  int32_t slice =
      static_cast<int32_t>(shift ? (dividend >> (ulpExpWithShiftD - 1))
                                : (dividend >> (ulpExpNoShiftD - 1)));

  if (slice >= divisorSlice3) {         // q_1 = 2
    auto subtrahend = divisor << 1;
    CPUstate.DIVModule.regS = (dividend ^ ~subtrahend ^ 1) << 2;
    CPUstate.DIVModule.regC = ((dividend & ~subtrahend) | (dividend & 1) |
            (~subtrahend & 1))
           << 3;
    CPUstate.DIVModule.regA = 2;
    CPUstate.DIVModule.regB = 1;
  } else if (slice >= divisorSlice) { // q_1 = 1
    auto subtrahend = divisor;
    CPUstate.DIVModule.regS = (dividend ^ ~subtrahend ^ 1) << 2;
    CPUstate.DIVModule.regC = ((dividend & ~subtrahend) | (dividend & 1) |
            (~subtrahend & 1))
           << 3;
    CPUstate.DIVModule.regA = 1;
    CPUstate.DIVModule.regB = 0;
  } else { // q_1 = 0: no subtrahend
    CPUstate.DIVModule.regS = dividend << 2;
    CPUstate.DIVModule.regC = 0;
    CPUstate.DIVModule.regA = 0;
    CPUstate.DIVModule.regB = 3;
  }
  CPUstate.DIVModule.loopValid = (loops != 0);
  CPUstate.DIVModule.fullAdderValid = (loops == 0);
}
void DIV::loop(uint64_t oldRegS, uint64_t oldRegC, uint32_t oldRegA,
               uint32_t oldRegB, systemState &CPUstate) const {
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
    CPUstate.DIVModule.regS = (S4 ^ C4 ^ T) & mask;
    CPUstate.DIVModule.regC = ((((S4 & C4) | (S4 & T) | (C4 & T)) << 1) | 1) & mask;
    CPUstate.DIVModule.regA = (oldRegA << 2) | 2;
    CPUstate.DIVModule.regB = (oldRegA << 2) | 1;
  } else if (slice >= dSlice) { // q = +1
    uint64_t subtrahend = unsignedDivisor << 2;
    uint64_t T = mask ^ subtrahend;
    CPUstate.DIVModule.regS = (S4 ^ C4 ^ T) & mask;
    CPUstate.DIVModule.regC = ((((S4 & C4) | (S4 & T) | (C4 & T)) << 1) | 1) & mask;
    CPUstate.DIVModule.regA = (oldRegA << 2) | 1;
    CPUstate.DIVModule.regB = (oldRegA << 2) | 0;
  } else if (slice >= -dSlice) { // q = 0: no subtrahend
    CPUstate.DIVModule.regS = (S4 ^ C4) & mask;
    CPUstate.DIVModule.regC = ((S4 & C4) << 1) & mask;
    CPUstate.DIVModule.regA = oldRegA << 2;
    CPUstate.DIVModule.regB = (oldRegB << 2) | 3;
  } else if (slice >= -dSlice3) { // q = -1
    uint64_t subtrahend = unsignedDivisor << 2;
    CPUstate.DIVModule.regS = (S4 ^ C4 ^ subtrahend) & mask;
    CPUstate.DIVModule.regC = (((S4 & C4) | (S4 & subtrahend) | (C4 & subtrahend)) << 1) & mask;
    CPUstate.DIVModule.regA = (oldRegB << 2) | 3;
    CPUstate.DIVModule.regB = (oldRegB << 2) | 2;
  } else { // q = -2
    uint64_t subtrahend = unsignedDivisor << 3;
    CPUstate.DIVModule.regS = (S4 ^ C4 ^ subtrahend) & mask;
    CPUstate.DIVModule.regC = (((S4 & C4) | (S4 & subtrahend) | (C4 & subtrahend)) << 1) & mask;
    CPUstate.DIVModule.regA = (oldRegB << 2) | 2;
    CPUstate.DIVModule.regB = (oldRegB << 2) | 1;
  }
  CPUstate.DIVModule.loopTimes = loopTimes - 1;
  if (loopTimes != 1) {
    CPUstate.DIVModule.loopValid = true;
  } else {
    CPUstate.DIVModule.loopValid = false;
    CPUstate.DIVModule.fullAdderValid = true;
  }
}
void DIV::calculateResult(uint64_t oldRegS, uint64_t oldRegC, uint32_t oldRegA,
                          uint32_t oldRegB, systemState &CPUstate) const {
  uint64_t Pk = oldRegS + oldRegC;
  Pk &= shiftD ? (1ull << 36) - 1 : (1ull << 35) - 1;
  if ((shiftD && (Pk >> 35) & 1) || (!shiftD && (Pk >> 34) & 1)) {
    Pk -= shiftD ? 1ull << 36 : 1ull << 35;
  }
  if ((Pk >> 63) == 0) {
    CPUstate.DIVModule.quotient = oldRegA;
    CPUstate.DIVModule.remain = ((Pk >> 2) >> shiftD) >> clzD;
  } else {
    CPUstate.DIVModule.quotient = oldRegB;
    CPUstate.DIVModule.remain = (((Pk + (unsignedDivisor << 2)) >> 2) >> shiftD) >> clzD;
  }
  CPUstate.DIVModule.fullAdderValid = false;
  CPUstate.DIVModule.resultValid = true;
}
void DIV::flush(uint8_t tag, RobTag effectiveTag, systemState &CPUstate) const {
  if (ROB::isOlder(tag, effectiveTag)) {
    CPUstate.DIVModule.unsignedDivisor = 0;
    CPUstate.DIVModule.unsignedDividend = 0;
    CPUstate.DIVModule.prepareValid = 0;
    CPUstate.DIVModule.isDividendNegative = 0;
    CPUstate.DIVModule.isResultNegative = 0;
    CPUstate.DIVModule.clzX = 0;
    CPUstate.DIVModule.clzD = 0;
    CPUstate.DIVModule.loopTimes = 0;
    CPUstate.DIVModule.regS = 0;
    CPUstate.DIVModule.regC = 0;
    CPUstate.DIVModule.regA = 0;
    CPUstate.DIVModule.regB = 0;
    CPUstate.DIVModule.dSlice = 0;
    CPUstate.DIVModule.dSlice3 = 0;
    CPUstate.DIVModule.loopValid = 0;
    CPUstate.DIVModule.quotient = 0;
    CPUstate.DIVModule.remain = 0;
    CPUstate.DIVModule.robTag = 0;
    CPUstate.DIVModule.fullAdderValid = 0;
    CPUstate.DIVModule.shiftD = 0;
    CPUstate.DIVModule.resultValid = 0;
  }
}
void DIV::tick(const DIVInput &input, systemState &CPUstate) {
  if (input.cdbOutput.valid) {
    CPUstate.DIVModule.resultValid = false;
  } // consume the result by cdb
  if (fullAdderValid) {
    calculateResult(regS, regC, regA, regB, CPUstate);
  }
  if (loopValid) {
    loop(regS, regC, regA, regB, CPUstate);
  }
  if (prepareValid) {
    prepare(CPUstate);
  }
  if (input.dispatch.valid) {
    const auto &rs = input.RSModule.divideRS[input.dispatch.rsIndex];
    receive(
        static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src1)),
        static_cast<uint32_t>(input.PRFModule.getOperandValue(rs.src2)),
        input.dispatch.robTag, rs.op, CPUstate);
  }
  if (input.squashDetect.needSquash) {
    flush(input.squashDetect.SquashTag, input.dispatch.valid ? input.dispatch.robTag : robTag, CPUstate);
  }
}
