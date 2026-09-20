#pragma once
#include "CDB.hpp"
#include "RS.hpp"
#include "common.hpp"
#include <cstdint>
#include <stdexcept>
constexpr int ulpExpWithShiftD = 28;
constexpr int ulpExpNoShiftD = 27;
constexpr int sliceShiftWithShiftD = 27;
constexpr int sliceShiftNoShiftD = 26;
struct DIVInput {
  SquashInfo squashDetect;
  const RSUnit &RSModule;
  const PRF &PRFModule;
  divCDB cdbOutput;
  DispatchInfo dispatch;
  DIVInput(const RSUnit &rs, const PRF &prf) : RSModule(rs), PRFModule(prf) {}
};
class DIV {
  friend struct DIVTester; // warehouse-external probe (same convention as
                           // MULTester in MUL.hpp); never used by the CPU.
private:
  // Every member is default-initialised: systemState is built with `= default`,
  // so an uninitialised DIV would let divCDB::build broadcast a ghost result
  // on cycle 0 (MUL avoids this with its slotValid[] array).
  uint64_t unsignedDivisor = 0;
  uint64_t unsignedDividend = 0;
  bool prepareValid = false;
  bool isDividendNegative = false;
  bool isResultNegative = false;
  uint8_t clzX = 0;
  uint8_t clzD = 0;
  uint8_t loopTimes = 0;
  uint64_t regS = 0;
  uint64_t regC = 0;
  uint32_t regA = 0;
  uint32_t regB = 0;
  int32_t dSlice = 0;
  int32_t dSlice3 = 0;
  bool loopValid = false;
  uint32_t quotient = 0;
  uint32_t remain = 0;
  uint8_t robTag = 0;
  Operation operationType = Operation::OP_INVALID;
  bool fullAdderValid = false;
  bool shiftD = false;
  bool resultValid = false;
  void receive(uint32_t op1, uint32_t op2, RobTag robTag, Operation op);
  void prepare();
  void loop(uint64_t oldRegS, uint64_t oldRegC, uint32_t oldRegA,
            uint32_t oldRegB);
  void calculateResult(uint64_t oldRegS, uint64_t oldRegC, uint32_t oldRegA,
                       uint32_t oldRegB);
  void flush(uint8_t tag);

public:
  // The CPU drives the divider the same way it drives the MUL: one tick() per
  // cycle, reading the local snapshot and writing CPUstate.DIVModule.
  void tick(const DIVInput &, systemState &);
  bool isReady() const { return resultValid; }
  // The divider is a single iterative unit (no output buffer like MUL), so it
  // must not be handed a new op while it is still computing OR while a
  // finished result has not been broadcast yet.
  bool canAccept() const {
    return !prepareValid && !loopValid && !fullAdderValid && !resultValid;
  }
  uint8_t getResultRobtag() const { return robTag; }
  uint32_t getValue() const {
    // Results are uint32 bit vectors. The sign fixups run on uint32 so that
    // INT32_MIN never invokes host signed negate UB (no -fwrapv).
    if (operationType == Operation::DIV) {
      return isResultNegative ? 0u - quotient : quotient;
    }
    if (operationType == Operation::DIVU) {
      return quotient;
    }
    if (operationType == Operation::REM) {
      return isDividendNegative ? 0u - remain : remain;
    }
    if (operationType == Operation::REMU) {
      return remain;
    }
    throw std::runtime_error("not DIV operation!");
  }
};
