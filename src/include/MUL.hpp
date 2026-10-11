#pragma once
#include "CDB.hpp"
#include "RS.hpp"
#include "common.hpp"
#include <cstdint>
struct systemState;
struct PRF;
struct MULInput {
  SquashInfo squashDetect;
  const RSUnit &RSModule;
  const PRF &PRFModule;
  mulCDB cdbOutput;
  DispatchInfo dispatch;
  MULInput(const RSUnit &rs, const PRF &prf) : RSModule(rs), PRFModule(prf) {}
};
class MUL {

private:
  struct MultiplyCalculateResult {
    uint32_t value = 0;
    uint8_t robTag = 0;
  };
  struct PartialProductResult {
    // 19 rows, all full-width 64-bit values:
    //   [0..15]  radix-4 Booth digit rows of op2; a negative digit keeps the
    //            one's complement of the positive multiple in its row,
    //   [16..17] unsigned-operand fixups for MULHU / MULHSU,
    //   [18]     sparse +1 corrections: one bit at column 2i per negative-
    //            digit row, completing each row's one's complement into the
    //            two's complement it needs (classic Booth neg bit).
    // In the full 64-bit domain the one's complement already carries its own
    // sign extension, so no truncated-field repayment row is required.
    uint64_t partialProduct[19] = {};
    uint8_t robTag = 0;
    Operation op = Operation::MUL;
    bool partialProductValid = false;
  };
  PartialProductResult partialRes;
  struct SCResult {
    uint64_t S = 0;
    uint64_t C = 0;
    bool carryAdderValid = false;
    uint8_t robTag = 0;
    Operation op = Operation::MUL;
  };
  SCResult scRes;
  MultiplyCalculateResult outputBuffer[MUL_CAP];
  bool slotValid[MUL_CAP] = {};
  void calculateBooth(uint32_t op1, uint32_t op2, RobTag robTag, Operation op, systemState &CPUstate) const;
  void calculateSC(const PartialProductResult &partial, systemState &CPUstate) const;
  void calculateMulRes(const SCResult &sc, systemState &CPUstate) const;

public:
  bool resultCanAccept(const mulCDB &grant, const SquashInfo &squash) const;
  bool CSACanAccept(const mulCDB &grant, const SquashInfo &squash) const;
  bool partialCanAccept(const mulCDB &grant, const SquashInfo &squash) const;
  bool isFull() const;
  bool isEmpty() const;
  uint32_t headValue() const;
  uint8_t headRobTag() const;
  void tick(const MULInput &, systemState &);
};
static_assert(MUL_CAP == 1);
