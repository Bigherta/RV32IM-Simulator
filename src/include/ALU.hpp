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
  DispatchInfo dispatch;
  ALUInput(const RSUnit &rs, const PRF &prf) : RSModule(rs), PRFModule(prf) {}
};
class ALU {
private:
  struct ArithmeticCalculateResult {
    uint32_t value = 0;
    uint8_t robTag = 0;
    bool isControl = false;
  };
  ArithmeticCalculateResult outputBuffer[ALU_CAP];
  bool slotValid[ALU_CAP] = {};
  void push(uint32_t op1, uint32_t op2, Operation op, RobTag robTag,
            bool isControl);
  void remove(uint8_t robTag);
  void flush(uint8_t tag);

public:
  bool isFull() const;
  bool isEmpty() const;
  uint32_t headValue() const;
  uint8_t headRobTag() const;
  bool headIsControl() const;
  void tick(const ALUInput &, systemState &);
};
