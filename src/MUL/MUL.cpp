#include "../include/MUL.hpp"
#include "../include/CPU.hpp"
#include "../include/common.hpp"
#include <cassert>
#include <cstdint>

namespace {
// 3:2 CSA
struct Csa3 {
  uint64_t sum;
  uint64_t carry;
};
inline Csa3 csa3(uint64_t a, uint64_t b, uint64_t c) {
  Csa3 r;
  r.sum = a ^ b ^ c;
  // majority(a,b,c) << 1: the explicit parens matter -- `|` binds looser
  // than `<<`, so the three AND terms must be grouped before the shift.
  r.carry = ((a & b) | (a & c) | (b & c)) << 1;
  return r;
}
} // namespace

void MUL::calculateBooth(int32_t op1, int32_t op2, RobTag robTag,
                         Operation op) {
  partialRes.partialProductValid = true;
  partialRes.op = op;
  partialRes.robTag = robTag;
  for (int i = 0; i < 19; ++i)
    partialRes.partialProduct[i] = 0;
  const uint64_t A = static_cast<uint64_t>(op1);
  for (int i = 0; i < 16; ++i) {
    // 3-bit window y[2i+1], y[2i], y[2i-1]; row 0 pads y[-1] = 0.
    const uint32_t triple =
        (i == 0) ? ((static_cast<uint32_t>(op2) & 0b11) << 1)
                 : ((static_cast<uint32_t>(op2) >> ((i << 1) - 1)) & 0b111);

    uint64_t row = 0; // |digit| multiple of A, before the sign handling
    bool neg = false;
    switch (triple) {
    case 0b001:
    case 0b010: // digit +1
      row = A;
      break;
    case 0b011: // digit +2
      row = A << 1;
      break;
    case 0b100: // digit -2
      row = A << 1;
      neg = true;
      break;
    case 0b101:
    case 0b110: // digit -1
      row = A;
      neg = true;
      break;
    default: // 000 / 111: digit 0
      break;
    }
    if (neg) {
      row = ~row;
      partialRes.partialProduct[18] |= (1ULL << (i << 1));
    }
    partialRes.partialProduct[i] = row << (i << 1);
  }
  // Unsigned-operand fixups.
  const uint64_t signA = (static_cast<uint32_t>(op1) >> 31) & 1;
  const uint64_t signB = (static_cast<uint32_t>(op2) >> 31) & 1;
  const bool isMulhu = (op == Operation::MULHU);
  const bool isMulhsu = (op == Operation::MULHSU);
  partialRes.partialProduct[16] =
      (isMulhu && signA)
          ? (static_cast<uint64_t>(static_cast<uint32_t>(op2)) << 32)
          : 0;
  partialRes.partialProduct[17] =
      ((isMulhu || isMulhsu) && signB)
          ? (static_cast<uint64_t>(static_cast<int64_t>(op1)) << 32)
          : 0;
  uint64_t expected;
  switch (op) {
  case Operation::MUL:
  case Operation::MULH:
    expected = (uint64_t)(int64_t)op1 * (uint64_t)(int64_t)op2;
    break;
  case Operation::MULHU:
    expected = (uint64_t)(uint32_t)op1 * (uint64_t)(uint32_t)op2;
    break;
  case Operation::MULHSU:
    expected = (uint64_t)(int64_t)op1 * (uint64_t)(uint32_t)op2;
    break;
  default:
    expected = 0;
    break;
  }
  partialRes.expected = expected;
#ifndef NDEBUG
  // Booth-level self check (isolates row-generation / negation bugs): the
  // full 19-row deposit must already equal the reference product mod 2^64.
  uint64_t rowSum = 0;
  for (int i = 0; i < 19; ++i)
    rowSum += partialRes.partialProduct[i];
  assert(rowSum == partialRes.expected);
#endif
}

void MUL::calculateSC(const PartialProductResult &partial) {
  scRes.carryAdderValid = true;
  scRes.op = partial.op;
  scRes.robTag = partial.robTag;
  const auto &P = partial.partialProduct; // 19 rows, all consumed
  // 3:2 compressor tree: 19 -> 13 -> 9 -> 6 -> 4 -> 3 -> 2 (17 cells)
  Csa3 a0 = csa3(P[0], P[1], P[2]);
  Csa3 a1 = csa3(P[3], P[4], P[5]);
  Csa3 a2 = csa3(P[6], P[7], P[8]);
  Csa3 a3 = csa3(P[9], P[10], P[11]);
  Csa3 a4 = csa3(P[12], P[13], P[14]);
  Csa3 a5 = csa3(P[15], P[16], P[17]);
  Csa3 b0 = csa3(a0.sum, a0.carry, a1.sum);
  Csa3 b1 = csa3(a1.carry, a2.sum, a2.carry);
  Csa3 b2 = csa3(a3.sum, a3.carry, a4.sum);
  Csa3 b3 = csa3(a4.carry, a5.sum, a5.carry);
  Csa3 c0 = csa3(b0.sum, b0.carry, b1.sum);
  Csa3 c1 = csa3(b1.carry, b2.sum, b2.carry);
  Csa3 c2 = csa3(b3.sum, b3.carry, P[18]);
  Csa3 d0 = csa3(c0.sum, c0.carry, c1.sum);
  Csa3 d1 = csa3(c1.carry, c2.sum, c2.carry);
  Csa3 e0 = csa3(d0.sum, d0.carry, d1.sum);
  Csa3 f0 = csa3(e0.sum, e0.carry, d1.carry);
  scRes.S = f0.sum;
  scRes.C = f0.carry;
#ifndef NDEBUG
  // Tree-level check (isolates compression/rounding bugs): S + C must still
  // equal the reference product, independently of the Booth-level check.
  assert(scRes.S + scRes.C == partial.expected);
#endif
}

void MUL::calculateMulRes(const SCResult &sc) {
  uint64_t res = sc.S + sc.C; // full 64-bit product (mod 2^64)
  int best = -1;
  for (int i = 0; i < MUL_CAP; ++i) {
    if (!slotValid[i]) {
      best = i;
      break;
    }
  }
  // canAccept() invariant watchdog: a full buffer here means dispatch was
  // granted faster than the dedicated cdbOfMul bus drained it.
  assert(best != -1 && "MUL slot overflow: canAccept invariant broken");
  outputBuffer[best].robTag = sc.robTag;
  slotValid[best] = true;
  switch (sc.op) {
  case Operation::MUL: {
    outputBuffer[best].value = static_cast<int32_t>(res);
    break;
  }
  case Operation::MULH:
  case Operation::MULHSU:
  case Operation::MULHU: {
    outputBuffer[best].value = static_cast<int32_t>(res >> 32);
    break;
  }
  default:
    break;
  }
}

int32_t MUL::headValue() const {
  int best = -1;
  for (int i = 0; i < MUL_CAP; i++) {
    if (slotValid[i] && (best == -1 || ROB::isOlder(outputBuffer[i].robTag,
                                                    outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].value : 0;
}
uint8_t MUL::headRobTag() const {
  int best = -1;
  for (int i = 0; i < MUL_CAP; i++) {
    if (slotValid[i] && (best == -1 || ROB::isOlder(outputBuffer[i].robTag,
                                                    outputBuffer[best].robTag)))
      best = i;
  }
  return best >= 0 ? outputBuffer[best].robTag : 0;
}
bool MUL::isFull() const {
  for (int i = 0; i < MUL_CAP; i++) {
    if (!slotValid[i])
      return false;
  }
  return true;
}

bool MUL::isEmpty() const {
  for (int i = 0; i < MUL_CAP; i++) {
    if (slotValid[i])
      return false;
  }
  return true;
}

void MUL::remove(uint8_t robTag) {
  for (int i = 0; i < MUL_CAP; i++) {
    if (slotValid[i] && outputBuffer[i].robTag == robTag) {
      slotValid[i] = false;
      return;
    }
  }
}

void MUL::flush(uint8_t tag) {
  for (int i = 0; i < MUL_CAP; i++) {
    if (slotValid[i] && !ROB::isOlder(outputBuffer[i].robTag, tag))
      slotValid[i] = false;
  }
  if (ROB::isOlder(tag, partialRes.robTag)) {
    partialRes.partialProductValid = false;
  }
  if (ROB::isOlder(tag, scRes.robTag)) {
    scRes.carryAdderValid = false;
  }
}
void MUL::tick(const MULInput &input, systemState &CPUstate) {
  auto &mul = CPUstate.MULModule;
  if (input.cdbOutput.valid) {
    mul.remove(input.cdbOutput.robTag);
  }

  if (scRes.carryAdderValid) {
    mul.calculateMulRes(scRes);
    mul.scRes.carryAdderValid = false;
  }

  if (partialRes.partialProductValid) {
    mul.calculateSC(partialRes);
    mul.partialRes.partialProductValid = false;
  } else {
    mul.scRes.carryAdderValid = false;
  }

  if (input.dispatch.valid) {
    const auto &rs = input.RSModule.multiplyRS[input.dispatch.rsIndex];
    mul.calculateBooth(input.PRFModule.getOperandValue(rs.src1),
                       input.PRFModule.getOperandValue(rs.src2),
                       input.dispatch.robTag, rs.op);
  } else {
    mul.partialRes.partialProductValid = false;
  }

  if (input.squashDetect.needSquash) {
    mul.flush(input.squashDetect.SquashTag);
  }
}
