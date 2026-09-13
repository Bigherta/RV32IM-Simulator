#pragma once
#include <cstdint>

struct ALU;
struct MUL;
struct DIV;
struct LQ;
struct SquashInfo;

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
struct divCDB {
  int32_t value = 0;
  uint8_t robTag = 0;
  bool valid = false;
  static divCDB build(const DIV &div, const SquashInfo &squash);
};
struct lqCDB {
  int32_t value = 0;
  uint8_t robTag = 0;
  uint8_t memIndex = 0;
  bool valid = false;
  static lqCDB build(const LQ &lq, const SquashInfo &squash);
};
