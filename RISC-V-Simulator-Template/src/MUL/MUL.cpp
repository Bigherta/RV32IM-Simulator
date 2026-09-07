#include "../include/MUL.hpp"
#include "../include/ROB.hpp"
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
// Re-join a 64-bit pipeline value from its lo/hi 32-bit register halves.
// Pure combinational: no storage, never crosses a cycle boundary.
inline uint64_t join64(uint32_t hi, uint32_t lo) {
  return (static_cast<uint64_t>(hi) << 32) | lo;
}
} // namespace

// ---------------------------------------------------------------------------
// All stage logic lives in work() ("always_ff" body). 64-bit math uses
// uint64_t locals only: Booth row generation, the compressor tree and the
// final carry-propagate add are pure combinational in RTL (zero flip-flops).
// What crosses a cycle boundary is stored as Row64 (lo/hi Register<32>
// pair, max_size_t caps stored state at 32 bits) and re-joined here.
// Single-assignment per Register: push/fill/remove/flush converge in the
// tail sections, so squash and dispatch on the same cycle never
// double-write.
// ---------------------------------------------------------------------------
void MUL::work() {
  // ---- 0. sampling: input wires + committed (_M_old) stage state.
  // Register reads are cycle-stable, so helper-free ordering below is
  // irrelevant to the result; section order only mirrors the reference tick.
  const bool squash = static_cast<bool>(needSquash);
  const uint32_t squashTag = static_cast<uint32_t>(SquashTag);
  const bool dValid = static_cast<bool>(dispatchValid);
  const uint32_t dTag = static_cast<uint32_t>(dispatchRobTag);
  const Operation dOp = static_cast<Operation>(static_cast<uint32_t>(op));
  const bool grant = static_cast<bool>(cdbValid);
  const uint32_t cdbTag = static_cast<uint32_t>(cdbRobTag);

  const bool boothOld = static_cast<bool>(partialRes.valid);
  const bool scOld = static_cast<bool>(scRes.valid);
  const RobTag partialTag =
      static_cast<uint8_t>(static_cast<uint32_t>(partialRes.robTag));
  const RobTag scTag =
      static_cast<uint8_t>(static_cast<uint32_t>(scRes.robTag));
  const Operation scOp =
      static_cast<Operation>(static_cast<uint32_t>(scRes.op));
  const Operation partialOp =
      static_cast<Operation>(static_cast<uint32_t>(partialRes.op));

  // ---- 1. stage 3 (MulRes): re-join the carry-save pair, final
  // carry-propagate add, half select, push the result into a free slot.
  int filled = -1;
  if (scOld) {
    const uint64_t sOld = join64(static_cast<uint32_t>(scRes.S_hi),
                                 static_cast<uint32_t>(scRes.S_lo));
    const uint64_t cOld = join64(static_cast<uint32_t>(scRes.C_hi),
                                 static_cast<uint32_t>(scRes.C_lo));
    const uint64_t res = sOld + cOld; // full 64-bit product (mod 2^64)
    // Free-slot scan mirroring the reference's live bitmap: its tick ran
    // remove() before the scan, so a slot being drained this cycle
    // (grant && tag match) is already reusable here.
    bool found = false;
    for (int i = 0; i < MUL_CAP; ++i) {
      const bool old_v = static_cast<bool>(slotValid[i]);
      const bool removed =
          grant && old_v &&
          static_cast<uint32_t>(slots[i].robTag) == cdbTag;
      if (!found && (!old_v || removed)) {
        filled = i;
        found = true;
      }
    }
    // canAccept() invariant watchdog: a full buffer here means dispatch was
    // granted faster than the dedicated cdbOfMul bus drained it.
    assert(filled != -1 && "MUL slot overflow: canAccept invariant broken");
    slots[filled].robTag <= static_cast<uint32_t>(scTag);
    slots[filled].value <= ((scOp == Operation::MUL)
                                ? static_cast<uint32_t>(res)
                                : static_cast<uint32_t>(res >> 32));
  }

  // ---- 2. stage 2 (SC): re-join the latched Booth rows, 3:2 compressor
  // tree, latch the carry-save pair.
  if (boothOld) {
    uint64_t P[19];
    for (int i = 0; i < 19; ++i)
      P[i] = join64(static_cast<uint32_t>(partialRes.rows[i].hi),
                    static_cast<uint32_t>(partialRes.rows[i].lo));
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
    scRes.S_lo <= static_cast<uint32_t>(f0.sum);
    scRes.S_hi <= static_cast<uint32_t>(f0.sum >> 32);
    scRes.C_lo <= static_cast<uint32_t>(f0.carry);
    scRes.C_hi <= static_cast<uint32_t>(f0.carry >> 32);
    scRes.op <= static_cast<uint32_t>(partialOp);
    scRes.robTag <= static_cast<uint32_t>(partialTag);
  }

  // ---- 3. stage 1 (Booth): latch the newly dispatched op into rows.
  // radix-4 digit rows of the multiplier with unsigned-operand fixups.
  if (dValid) {
    const int32_t op1 =
        static_cast<int32_t>(static_cast<uint32_t>(src1Value));
    const int32_t op2 =
        static_cast<int32_t>(static_cast<uint32_t>(src2Value));
    const uint64_t A = static_cast<uint64_t>(op1); // sign-extending: the
    // positive-digit rows inherit A's sign extension, which the MULH/MULHSU
    // high halves depend on when op1 < 0. Do NOT route through uint32_t
    // (zero extension breaks the high half; LCG gate caught this once).
    uint64_t row18 = 0; // sparse +1 corrections: one bit at column 2i per
                        // negative-digit row (classic Booth neg bits)
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
        row18 |= (1ULL << (i << 1));
      }
      const uint64_t shifted = row << (i << 1);
      partialRes.rows[i].lo <= static_cast<uint32_t>(shifted);
      partialRes.rows[i].hi <= static_cast<uint32_t>(shifted >> 32);
    }
    // [16] folds in zero-extended op2 when A was unsigned (MULHU); [17]
    // folds in sign-extended op1 when B was unsigned (MULHU/MULHSU). In the
    // full 64-bit domain the one's complement already carries its own sign
    // extension, so no truncated-field repayment row is required beyond
    // the row18 corrections above.
    const uint64_t signA = (static_cast<uint32_t>(op1) >> 31) & 1;
    const uint64_t signB = (static_cast<uint32_t>(op2) >> 31) & 1;
    const bool isMulhu = (dOp == Operation::MULHU);
    const bool isMulhsu = (dOp == Operation::MULHSU);
    const uint64_t fixup16 =
        (isMulhu && signA)
            ? (static_cast<uint64_t>(static_cast<uint32_t>(op2)) << 32)
            : 0;
    const uint64_t fixup17 =
        ((isMulhu || isMulhsu) && signB)
            ? (static_cast<uint64_t>(static_cast<int64_t>(op1)) << 32)
            : 0;
    partialRes.rows[16].lo <= static_cast<uint32_t>(fixup16);
    partialRes.rows[16].hi <= static_cast<uint32_t>(fixup16 >> 32);
    partialRes.rows[17].lo <= static_cast<uint32_t>(fixup17);
    partialRes.rows[17].hi <= static_cast<uint32_t>(fixup17 >> 32);
    // row18 bits stay below bit 31, the hi half is constant zero.
    partialRes.rows[18].lo <= static_cast<uint32_t>(row18);
    partialRes.rows[18].hi <= 0;
    partialRes.op <= static_cast<uint32_t>(dOp);
    partialRes.robTag <= static_cast<uint32_t>(dTag);
  }

  // ---- 4. stage valids: single write each. Squash kills a stage result
  // by the tag it will carry out of this cycle (stage 1: the just-latched
  // tag; stage 2: the tag handed over from partialRes) -- the reference's
  // tick flushed after the payload had been overwritten.
  const bool boothFlushed = squash && !ROB::isOlder(dTag, squashTag);
  const bool scFlushed = squash && !ROB::isOlder(partialTag, squashTag);
  partialRes.valid <= (dValid && !boothFlushed);
  scRes.valid <= (boothOld && !scFlushed);

  // ---- 5. output slots: flush > fill > remove > hold per slot. The
  // fill's flush check uses the (old) sc tag written this cycle.
  const bool fillFlushed = squash && !ROB::isOlder(scTag, squashTag);
  for (uint32_t i = 0; i < MUL_CAP; ++i) {
    const bool old_v = static_cast<bool>(slotValid[i]);
    const uint32_t tag_i = static_cast<uint32_t>(slots[i].robTag);
    const bool removed = grant && old_v && tag_i == cdbTag;
    const bool flushed =
        squash && old_v && !ROB::isOlder(tag_i, squashTag);
    const bool here = filled >= 0 && static_cast<uint32_t>(filled) == i;
    slotValid[i] <= (here ? !fillFlushed : (old_v && !removed && !flushed));
  }
}

bool MUL::isFull() const {
  for (int i = 0; i < MUL_CAP; i++)
    if (!static_cast<bool>(slotValid[i]))
      return false;
  return true;
}

bool MUL::isEmpty() const {
  for (int i = 0; i < MUL_CAP; i++)
    if (static_cast<bool>(slotValid[i]))
      return false;
  return true;
}

int32_t MUL::headValue() const {
  int best = -1;
  for (int i = 0; i < MUL_CAP; i++) {
    if (static_cast<bool>(slotValid[i]) &&
        (best == -1 ||
         ROB::isOlder(
             static_cast<uint8_t>(static_cast<uint32_t>(slots[i].robTag)),
             static_cast<uint8_t>(static_cast<uint32_t>(slots[best].robTag)))))
      best = i;
  }
  return best >= 0
             ? static_cast<int32_t>(static_cast<uint32_t>(slots[best].value))
             : 0;
}

uint8_t MUL::headRobTag() const {
  int best = -1;
  for (int i = 0; i < MUL_CAP; i++) {
    if (static_cast<bool>(slotValid[i]) &&
        (best == -1 ||
         ROB::isOlder(
             static_cast<uint8_t>(static_cast<uint32_t>(slots[i].robTag)),
             static_cast<uint8_t>(static_cast<uint32_t>(slots[best].robTag)))))
      best = i;
  }
  return best >= 0
             ? static_cast<uint8_t>(static_cast<uint32_t>(slots[best].robTag))
             : 0;
}
