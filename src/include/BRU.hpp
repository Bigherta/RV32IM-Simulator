#pragma once
#include "RS.hpp"
#include "common.hpp"
#include <cstdint>
struct systemState;
struct PRF;
struct BRUInput {
  SquashInfo squashDetect;
  const RSUnit &RSModule;
  const PRF &PRFModule;
  DispatchInfo dispatch;
  BRUInput(const RSUnit &rs, const PRF &prf)
      : RSModule(rs), PRFModule(prf) {}
};
class BRU {
private:
  BranchResult outputBuffer[BRU_CAP];
  bool slotValid[BRU_CAP] = {};
  void BRUExecute(uint32_t op1, uint32_t op2, uint32_t pc, uint32_t imm,
                  Operation op, RobTag robTag);
  void push(BranchResult);
  void remove(uint8_t robTag);
  void flush(uint8_t tag);

public:
  bool isFull() const;
  bool isEmpty() const;
  uint32_t headPCFrom() const;
  uint32_t headPCResult() const;
  uint8_t headRobTag() const;
  void tick(const BRUInput &, systemState &);
};
