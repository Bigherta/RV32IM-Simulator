#pragma once
#include "RS.hpp"
#include "common.hpp"
#include <cstdint>
struct systemState;
struct PRF;
struct BRUInput {
  SquashInfo squashDetect;
  const ROB &ROBModule;
  const RSUnit &RSModule;
  const PRF &PRFModule;
  DispatchInfo dispatch;
  BRUInput(const ROB &rob, const RSUnit &rs, const PRF &prf)
      : ROBModule(rob), RSModule(rs), PRFModule(prf) {}
};
class BRU {
private:
  BranchResult outputBuffer[BRU_CAP];
  bool slotValid[BRU_CAP] = {};
  void BRUExecute(int32_t op1, int32_t op2, int32_t pc, int32_t imm,
                  Operation op, RobTag robTag);
  void push(BranchResult);
  void remove(uint8_t robTag);
  void flush(uint8_t tag);

public:
  bool isFull() const;
  bool isEmpty() const;
  int32_t headPCFrom() const;
  int32_t headPCResult() const;
  uint8_t headRobTag() const;
  bool isValid(int index) const { return slotValid[index]; }
  void tick(const BRUInput &, systemState &);
};