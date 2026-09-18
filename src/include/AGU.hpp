#pragma once
#ifndef AGU_HPP
#define AGU_HPP
#include "common.hpp"
#include "RS.hpp"
struct systemState;
struct PRF;
struct AGUInput {
  SquashInfo squashDetect;
  const RSUnit &RSModule;
  const PRF &PRFModule;
  DispatchAGUInfo dispatch;
  AGUInput(const RSUnit &rs, const PRF &prf)
      : RSModule(rs), PRFModule(prf) {}
};
class AGU {
private:
  AddressCalculateResult outputBuffer[AGU_CAP];
  bool slotValid[AGU_CAP] = {};
  void push(int32_t op1, int32_t op2, RobTag robTag, uint8_t memIndex);
  void remove(uint8_t robTag);
  void flush(uint8_t tag);
public:
  bool isFull() const;
  bool isEmpty() const;
  int32_t headValue() const;
  uint8_t headRobTag() const;
  uint8_t headMemIndex() const;
  void tick(const AGUInput&, systemState&);
};
#endif // AGU_HPP
