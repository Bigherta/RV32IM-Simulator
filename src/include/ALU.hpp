#pragma once
#include "RS.hpp"
#include "common.hpp"
struct systemState;
struct PRF;
struct ALUInput {
  SquashInfo squashDetect;
  const RSUnit &RSModule;
  const PRF &PRFModule;
  aluCDB cdbOutput;
  std::array<bool, ALU_UNITS> popReady{};
  OperandWriteback operandWriteback;
  DispatchInfo dispatch;
  DispatchInfo dispatchSecond;
  ALUInput(const RSUnit &rs, const PRF &prf) : RSModule(rs), PRFModule(prf) {}
};
class ALU {
private:
  struct ArithmeticCalculateResult {
    uint32_t value = 0;
    uint8_t robTag = 0;
    bool isControl = false;
  };
  ArithmeticCalculateResult outputBuffer[ALU_UNITS];
  bool slotValid[ALU_UNITS] = {};
  void push(uint32_t op1, uint32_t op2, Operation op, RobTag robTag,
            bool isControl, bool keep, unsigned unit, const ALUInput &input, systemState &CPUstate) const;
  void remove(uint8_t robTag, systemState &CPUstate) const;
  void flush(uint8_t tag, systemState &CPUstate) const;

public:
  bool isFull(unsigned unit = 0) const;
  bool isEmpty(unsigned unit = 0) const;
  uint32_t headValue(unsigned unit = 0) const;
  uint8_t headRobTag(unsigned unit = 0) const;
  bool headIsControl(unsigned unit = 0) const;
  void tick(const ALUInput &, systemState &);
};
