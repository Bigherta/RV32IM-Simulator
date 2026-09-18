#include "../include/DIV.hpp"
#include "../include/ROB.hpp"
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
// Re-join a >32-bit pipeline value from its lo/hi register halves (MUL's
// join64 precedent: max_size_t caps stored state at 32 bits). Pure
// combinational -- no storage, never crosses a cycle boundary.
inline uint64_t join36(uint32_t hi, uint32_t lo) {
  return (static_cast<uint64_t>(hi & 0xFu) << 32) | lo;
}
inline uint64_t join33(uint32_t hi, uint32_t lo) {
  return (static_cast<uint64_t>(hi & 1u) << 32) | lo;
}
} // namespace
void DIV::receive(int32_t op1, int32_t op2, RobTag tag, Operation op) {
  // Stage 0 front-end (a): special cases (RISC-V semantics, return at once).
  // Parameters are (quotient, remain); the field this op does not produce is
  // written as 0. The signed paths' signs are already folded into the
  // patterns passed here (sec 2.1(b)), so no further sign fixup is needed.
  auto finish = [&](uint32_t quotientValue, uint32_t remainValue) {
    quotient <= quotientValue;
    remain <= remainValue;
    // The callers already fold the sign into the 32-bit pattern (sec 2.1(b)),
    // so getValue() must NOT negate again. These two flags are stale leftovers
    // from the previous instruction at this point -- clear them explicitly.
    isResultNegative <= false;
    isDividendNegative <= false;
    resultValid <= true;
    robTag <= tag;
    operationType <= static_cast<uint32_t>(op);
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
  resultValid <= false;
  robTag <= tag;
  operationType <= static_cast<uint32_t>(op);
  uint32_t xBits = static_cast<uint32_t>(op1);
  uint32_t dBits = static_cast<uint32_t>(op2);
  // signed magnitudes only for DIV/REM; DIVU/REMU keep the raw bit patterns
  const bool signedOp = (op == Operation::DIV || op == Operation::REM);
  bool rawIsDividendNegative = static_cast<bool>(signedOp && (op1 < 0));
  isDividendNegative <= rawIsDividendNegative;
  unsignedDividend <= (rawIsDividendNegative ? (~xBits + 1u) : xBits);
  bool rawIsDivisorNegative = static_cast<bool>(signedOp && (op2 < 0));
  // raw |d| < 2^32: the D_dp normalization shift happens in prepare().
  unsignedDivisorLo <= (rawIsDivisorNegative ? (~dBits + 1u) : dBits);
  unsignedDivisorHi <= 0;
  isResultNegative <= ((rawIsDivisorNegative ^ rawIsDividendNegative) ? 1 : 0);
  prepareValid <= true;
} // Dispatch is admitted only when canAccept() sees all stage flags and
  // resultValid low.
void DIV::prepare() {
  // Sampling: receive() wrote raw |x|/|d| last cycle (prepareValid is hot).
  // All same-cycle forwarding goes through raw* locals -- a Register read
  // always returns the committed _M_old view, never this cycle's <= writes.
  const uint32_t rawDividend = static_cast<uint32_t>(unsignedDividend);
  const uint32_t rawDivisor = static_cast<uint32_t>(unsignedDivisorLo);
  // Both clz inputs are nonzero here: the special cases intercepted d == 0
  // and |x| <= |d| in receive(), so both results are <= 31 (Register<5>).
  const uint32_t rawClzX = clz(rawDividend); // clzX = CLZ of |x|
  const uint32_t rawClzD = clz(rawDivisor);  // clzD = CLZ of |d|
  const uint32_t rawAlign = rawClzD - rawClzX;
  // ceil(align/2): prepare already consumed q_1, so loop() runs k-1 ticks
  const uint32_t rawLoopTimes = (rawAlign + 1) >> 1;
  // the 2^-shiftD factor lands on the divisor (sec 2.1(f))
  const uint32_t rawShiftD = rawAlign & 1;
  const uint64_t rawDividendNorm =
      static_cast<uint64_t>(rawDividend) << rawClzX; // P_1 = |x| << clzX
  const uint64_t rawDivisorDp = static_cast<uint64_t>(rawDivisor)
                                << (rawClzD + rawShiftD); // D_dp < 2^33
  const uint32_t rawDSlice = static_cast<uint32_t>(
      (rawDivisorDp >> (rawShiftD ? ulpExpWithShiftD : ulpExpNoShiftD)) & 31u);
  const uint32_t rawDSlice3 = rawDSlice + (rawDSlice << 1); // <= 93
  const int32_t rawSlice = static_cast<int32_t>(static_cast<uint32_t>(
      rawDividendNorm >>
      ((rawShiftD ? ulpExpWithShiftD : ulpExpNoShiftD) - 1)));

  // ---- single write per Register (the q_1 digit selection below reuses
  // only the raw* locals, never a Register written above) ----
  clzX <= rawClzX;
  clzD <= rawClzD;
  prepareValid <= false;
  unsignedDividend <= static_cast<uint32_t>(rawDividendNorm);
  unsignedDivisorLo <= static_cast<uint32_t>(rawDivisorDp);
  unsignedDivisorHi <= static_cast<uint32_t>(rawDivisorDp >> 32);
  loopTimes <= rawLoopTimes;
  shiftD <= rawShiftD;
  dSlice <= rawDSlice;
  dSlice3 <= rawDSlice3;

  if (rawSlice >= static_cast<int32_t>(rawDSlice3)) { // q_1 = 2
    const uint64_t rawSubtrahend = rawDivisorDp << 1; // q_1 * D_dp
    // Bits above the 36-bit P window carry one's-complement garbage; every
    // consumer masks to PW bits, so truncating the store (lo/hi pair) is
    // lossless vs the reference's full-width uint64_t state.
    const uint64_t rawRegS = (rawDividendNorm ^ ~rawSubtrahend ^ 1) << 2;
    const uint64_t rawRegC =
        ((rawDividendNorm & ~rawSubtrahend) | (rawDividendNorm & 1) |
         (~rawSubtrahend & 1))
        << 3;
    regSLo <= static_cast<uint32_t>(rawRegS);
    regSHi <= static_cast<uint32_t>(rawRegS >> 32);
    regCLo <= static_cast<uint32_t>(rawRegC);
    regCHi <= static_cast<uint32_t>(rawRegC >> 32);
    regA <= 2;
    regB <= 1;
  } else if (rawSlice >= static_cast<int32_t>(rawDSlice)) { // q_1 = 1
    const uint64_t rawSubtrahend = rawDivisorDp;
    const uint64_t rawRegS = (rawDividendNorm ^ ~rawSubtrahend ^ 1) << 2;
    const uint64_t rawRegC =
        ((rawDividendNorm & ~rawSubtrahend) | (rawDividendNorm & 1) |
         (~rawSubtrahend & 1))
        << 3;
    regSLo <= static_cast<uint32_t>(rawRegS);
    regSHi <= static_cast<uint32_t>(rawRegS >> 32);
    regCLo <= static_cast<uint32_t>(rawRegC);
    regCHi <= static_cast<uint32_t>(rawRegC >> 32);
    regA <= 1;
    regB <= 0;
  } else { // q_1 = 0: no subtrahend
    const uint64_t rawRegS = rawDividendNorm << 2;
    regSLo <= static_cast<uint32_t>(rawRegS);
    regSHi <= static_cast<uint32_t>(rawRegS >> 32);
    regCLo <= 0;
    regCHi <= 0;
    regA <= 0;
    regB <= 3;
  }
  loopValid <= (rawLoopTimes != 0);
  fullAdderValid <= (rawLoopTimes == 0);
}
void DIV::loop() {
  // Sampling: loop is the only writer this cycle, so the committed view of
  // every member is this op's live state.
  const bool rawShiftD = static_cast<bool>(shiftD);
  const int32_t rawDSlice =
      static_cast<int32_t>(static_cast<uint32_t>(dSlice));
  const int32_t rawDSlice3 =
      static_cast<int32_t>(static_cast<uint32_t>(dSlice3));
  const uint64_t rawDivisorDp = join33(
      static_cast<uint32_t>(unsignedDivisorHi),
      static_cast<uint32_t>(unsignedDivisorLo));
  const uint64_t oldRegS =
      join36(static_cast<uint32_t>(regSHi), static_cast<uint32_t>(regSLo));
  const uint64_t oldRegC =
      join36(static_cast<uint32_t>(regCHi), static_cast<uint32_t>(regCLo));
  const uint32_t oldRegA = static_cast<uint32_t>(regA);
  const uint32_t oldRegB = static_cast<uint32_t>(regB);

  // QDS: two 9-bit slices -> 9-bit two's complement -> drop LSB (= estPShift)
  uint32_t sum9 = rawShiftD ? ((oldRegS >> sliceShiftWithShiftD) & 0x1FFu) +
                                  ((oldRegC >> sliceShiftWithShiftD) & 0x1FFu)
                            : ((oldRegS >> sliceShiftNoShiftD) & 0x1FFu) +
                                  ((oldRegC >> sliceShiftNoShiftD) & 0x1FFu);
  int32_t slice = (int32_t)(((sum9 & 0x1FFu) ^ 0x100u) - 0x100u) & ~1;
  // registers hold P = 4W (PW = 35 + shiftD): shift first, then 3:2 compress
  const uint64_t mask = rawShiftD ? (1ull << 36) - 1 : (1ull << 35) - 1;
  const uint64_t S4 = (oldRegS << 2) & mask;
  const uint64_t C4 = (oldRegC << 2) & mask;
  if (slice >= rawDSlice3) { // q = +2
    const uint64_t subtrahend = rawDivisorDp << 3; // |q| * (D_dp << 2)
    const uint64_t T = mask ^ subtrahend; // ~qd; carry-in via regC bit0
    const uint64_t rawRegS = (S4 ^ C4 ^ T) & mask;
    const uint64_t rawRegC =
        ((((S4 & C4) | (S4 & T) | (C4 & T)) << 1) | 1) & mask;
    regSLo <= static_cast<uint32_t>(rawRegS);
    regSHi <= static_cast<uint32_t>(rawRegS >> 32);
    regCLo <= static_cast<uint32_t>(rawRegC);
    regCHi <= static_cast<uint32_t>(rawRegC >> 32);
    regA <= ((oldRegA << 2) | 2);
    regB <= ((oldRegA << 2) | 1);
  } else if (slice >= rawDSlice) { // q = +1
    const uint64_t subtrahend = rawDivisorDp << 2;
    const uint64_t T = mask ^ subtrahend;
    const uint64_t rawRegS = (S4 ^ C4 ^ T) & mask;
    const uint64_t rawRegC =
        ((((S4 & C4) | (S4 & T) | (C4 & T)) << 1) | 1) & mask;
    regSLo <= static_cast<uint32_t>(rawRegS);
    regSHi <= static_cast<uint32_t>(rawRegS >> 32);
    regCLo <= static_cast<uint32_t>(rawRegC);
    regCHi <= static_cast<uint32_t>(rawRegC >> 32);
    regA <= ((oldRegA << 2) | 1);
    regB <= ((oldRegA << 2) | 0);
  } else if (slice >= -rawDSlice) { // q = 0: no subtrahend
    const uint64_t rawRegS = (S4 ^ C4) & mask;
    const uint64_t rawRegC = ((S4 & C4) << 1) & mask;
    regSLo <= static_cast<uint32_t>(rawRegS);
    regSHi <= static_cast<uint32_t>(rawRegS >> 32);
    regCLo <= static_cast<uint32_t>(rawRegC);
    regCHi <= static_cast<uint32_t>(rawRegC >> 32);
    regA <= oldRegA << 2;
    regB <= ((oldRegB << 2) | 3);
  } else if (slice >= -rawDSlice3) { // q = -1
    const uint64_t subtrahend = rawDivisorDp << 2;
    const uint64_t rawRegS = (S4 ^ C4 ^ subtrahend) & mask;
    const uint64_t rawRegC =
        (((S4 & C4) | (S4 & subtrahend) | (C4 & subtrahend)) << 1) & mask;
    regSLo <= static_cast<uint32_t>(rawRegS);
    regSHi <= static_cast<uint32_t>(rawRegS >> 32);
    regCLo <= static_cast<uint32_t>(rawRegC);
    regCHi <= static_cast<uint32_t>(rawRegC >> 32);
    regA <= ((oldRegB << 2) | 3);
    regB <= ((oldRegB << 2) | 2);
  } else { // q = -2
    const uint64_t subtrahend = rawDivisorDp << 3;
    const uint64_t rawRegS = (S4 ^ C4 ^ subtrahend) & mask;
    const uint64_t rawRegC =
        (((S4 & C4) | (S4 & subtrahend) | (C4 & subtrahend)) << 1) & mask;
    regSLo <= static_cast<uint32_t>(rawRegS);
    regSHi <= static_cast<uint32_t>(rawRegS >> 32);
    regCLo <= static_cast<uint32_t>(rawRegC);
    regCHi <= static_cast<uint32_t>(rawRegC >> 32);
    regA <= ((oldRegB << 2) | 2);
    regB <= ((oldRegB << 2) | 1);
  }
  const uint32_t nextLoopTimes = static_cast<uint32_t>(loopTimes) - 1;
  loopTimes <= nextLoopTimes;
  if (nextLoopTimes != 0) {
    loopValid <= true;
  } else {
    loopValid <= false;
    fullAdderValid <= true;
  }
}
void DIV::calculateResult() {
  // Sampling: fullAdder is the only writer this cycle.
  const bool rawShiftD = static_cast<bool>(shiftD);
  const uint32_t rawClzD = static_cast<uint32_t>(clzD);
  const uint64_t rawDivisorDp = join33(
      static_cast<uint32_t>(unsignedDivisorHi),
      static_cast<uint32_t>(unsignedDivisorLo));
  const uint64_t oldRegS =
      join36(static_cast<uint32_t>(regSHi), static_cast<uint32_t>(regSLo));
  const uint64_t oldRegC =
      join36(static_cast<uint32_t>(regCHi), static_cast<uint32_t>(regCLo));
  const uint32_t oldRegA = static_cast<uint32_t>(regA);
  const uint32_t oldRegB = static_cast<uint32_t>(regB);

  uint64_t Pk = oldRegS + oldRegC;
  Pk &= rawShiftD ? (1ull << 36) - 1 : (1ull << 35) - 1;
  if ((rawShiftD && (Pk >> 35) & 1) || (!rawShiftD && (Pk >> 34) & 1)) {
    Pk -= rawShiftD ? 1ull << 36 : 1ull << 35;
  }
  if ((Pk >> 63) == 0) {
    quotient <= oldRegA;
    remain <= static_cast<uint32_t>(((Pk >> 2) >> rawShiftD) >> rawClzD);
  } else {
    quotient <= oldRegB;
    remain <=
        static_cast<uint32_t>((((Pk + (rawDivisorDp << 2)) >> 2) >> rawShiftD) >>
                              rawClzD);
  }
  fullAdderValid <= false;
  resultValid <= true;
}
void DIV::flush() {
  // Unconditional zeroing; work() owns the isOlder gate (effective-tag
  // semantics). operationType stays (verbatim): getValue() is only reachable
  // through the DivCDB gate when resultValid is set, so a stale op field is
  // unobservable.
  unsignedDivisorLo <= 0;
  unsignedDivisorHi <= 0;
  unsignedDividend <= 0;
  prepareValid <= 0;
  isDividendNegative <= 0;
  isResultNegative <= 0;
  clzX <= 0;
  clzD <= 0;
  loopTimes <= 0;
  regSLo <= 0;
  regSHi <= 0;
  regCLo <= 0;
  regCHi <= 0;
  regA <= 0;
  regB <= 0;
  dSlice <= 0;
  dSlice3 <= 0;
  loopValid <= 0;
  quotient <= 0;
  remain <= 0;
  robTag <= 0;
  fullAdderValid <= 0;
  shiftD <= 0;
  resultValid <= 0;
}
void DIV::work() {
  // ---- 0. sampling: cycle-stable input wires + committed state ----
  const bool squash = static_cast<bool>(needSquash);
  const uint32_t squashTag = static_cast<uint32_t>(SquashTag);
  const bool dValid = static_cast<bool>(dispatchValid);
  const uint32_t dTag = static_cast<uint32_t>(dispatchRobTag);
  const bool drain = static_cast<bool>(cdbValid);
  const uint32_t robTagOld = static_cast<uint32_t>(robTag);

  // The reference tick ran receive() before flush(), so a same-cycle dispatch
  // replaces the tag the squash test sees. The dispatch arbiter only grants
  // tags older than the squash point, so for a granted op the decision never
  // flips -- this equivalence clause only keeps the freshly received op
  // alive instead of losing it to a stale leftover robTag.
  const uint32_t effTag = dValid ? dTag : robTagOld;
  const bool flushFires = squash && ROB::isOlder(squashTag, effTag);

  // ---- single-writer priority chain. The branches are mutually exclusive by
  // construction: dispatch is granted only when canAccept() holds (idle, so
  // no stage flag is hot); drain implies resultValid, which excludes every
  // stage flag and dispatch; flush's write set is a superset of every other
  // branch's, so taking it alone matches the reference's last-write-wins. ----
  if (flushFires) {
    flush();
  } else if (drain) { // consume the result via the dedicated DivCDB bus
    resultValid <= false;
  } else if (static_cast<bool>(fullAdderValid)) {
    calculateResult();
  } else if (static_cast<bool>(loopValid)) {
    loop();
  } else if (static_cast<bool>(prepareValid)) {
    prepare();
  } else if (dValid) {
    receive(static_cast<int32_t>(static_cast<uint32_t>(src1Value)),
            static_cast<int32_t>(static_cast<uint32_t>(src2Value)),
            static_cast<RobTag>(dTag),
            static_cast<Operation>(static_cast<uint32_t>(op)));
  }
}
