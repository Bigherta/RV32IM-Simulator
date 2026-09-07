#pragma once
#include "../include/Memory.hpp"
#include "common.h"
#include "module.h"
#include "tools.h"
#include <array>

// Dual-port, line-granular data memory (2026-09-01).
//
// The DCache is DMEM's only client: it never issues Operation::Load/Store
// sub-word accesses anymore, only whole 16B-line reads (fill) and whole 16B
// line writes (dirty writeback). The two ports are fully independent -- a
// writeback and a fill can be in flight simultaneously, which is what lets a
// dirty-victim miss cost the same as a clean one.
//
// Line latency is a property of this module (not a field travelling on the
// request bus), so the request carries {valid, address} / {valid, address,
// lineData} only.

// DCache -> DMEM, one pulse per port per cycle (claimed on !busy).
struct DMEMInput {
  Wire<1> readValid;
  Wire<32> readAddress; // 16B-aligned block base
  Wire<1> writeValid;
  Wire<32> writeAddress; // 16B-aligned victim base
  std::array<Wire<8>, DCACHE_BLOCK_CAP> writeLineData;
};
// Registered execution pipelines (one per port) + the reply buffer.
struct DMEMInner {
  Register<1> readBusy;
  Register<6> execReadRemainCycle;
  Register<32> execReadAddress;
  Register<1> writeBusy;
  Register<6> execWriteRemainCycle;
  Register<32> execWriteAddress;
  std::array<Register<8>, DCACHE_BLOCK_CAP> execWriteLineData;
};
// DMEM -> DCache: the completed line read (one-cycle valid, pulled by the
// consumer the cycle after it is produced).
struct DMEMOutput {
  Register<1> replyValid;
  std::array<Register<8>, DCACHE_BLOCK_CAP> replyLineData;
};

// Data memory.
struct DMEM : public Memory, dark::Module<DMEMInput, DMEMOutput, DMEMInner> {
  DMEM() = default;
  DMEM(const Memory &mem) : Memory(mem) {}
  // Sub-word load, used by the reorder/consistency paths only (the DCache
  // reads whole lines through the read port).
  int32_t load_n_bytes(uint32_t address, int n, bool isSigned) const;
  void writeLine(uint32_t addr, const uint8_t *lineData);
  bool isReadBusy() const { return static_cast<bool>(readBusy); }
  bool isWriteBusy() const { return static_cast<bool>(writeBusy); }
  bool isReplyReady() const { return static_cast<bool>(replyValid); }
  // One byte of the completed line read (DCache fill path).
  uint8_t lineByte(int i) const {
    return static_cast<uint8_t>(static_cast<uint32_t>(replyLineData[i]));
  }
  void work() override;
};
