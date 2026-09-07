#pragma once
#include <cstdint>

struct ALU;
struct MUL;
struct LQ;
struct SquashInfo;

// 双 CDB 总线载荷：ALU / LQ 各驱动一根自己的总线，无跨单元仲裁。
// build() 工厂（CDB.cpp）选出该源的唯一候选并过 squash 门。
struct aluCDB {
  int32_t value = 0;
  uint8_t robTag = 0;
  bool isControl = false;
  bool valid = false;
  static aluCDB build(const ALU &alu, const SquashInfo &squash);
};
struct mulCDB {
  int32_t value = 0;
  uint8_t robTag = 0;
  bool valid = false;
  static mulCDB build(const MUL &mul, const SquashInfo &squash);
};
struct lqCDB {
  int32_t value = 0;
  uint8_t robTag = 0;
  uint8_t memIndex = 0;
  bool valid = false;
  static lqCDB build(const LQ &lq, const SquashInfo &squash);
};
