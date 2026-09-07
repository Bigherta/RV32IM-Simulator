#pragma once
#ifndef CPU_HPP
#define CPU_HPP
#include "AGU.hpp"
#include "ALU.hpp"
#include "DynamicArbiter.hpp"
#include "StaticArbiter.hpp"
#include "BRU.hpp"
#include "BPU.hpp"
#include "CDB.hpp"
#include "DCache.hpp"
#include "DMEM.hpp"
#include "Decoder.hpp"
#include "InstructBuffer.hpp"
#include "FetchUnit.hpp"
#include "ICache.hpp"
#include "IMEM.hpp"
#include "LQ.hpp"
#include "MUL.hpp"
#include "SQ.hpp"
#include "Memory.hpp"
#include "PRF.hpp"
#include "RAT.hpp"
#include "ROB.hpp"
#include "RS.hpp"
#include "cpu.h"
#include "common.h"
#include <cstring>

class CPU {
private:
  // Framework runner: run_once() = ++cycles -> registered work() sequence ->
  // sync_all(); run_once_shuffle() randomizes the work order (order
  // independence is structural: every cross-module read is a Wire tied to
  // the committed _M_old view). Registration order mirrors the former
  // hand-written run loop exactly.
  dark::CPU dcpu;
  RSUnit RSModule;
  RAT RATModule;
  ROB ROBModule;
  ALU ALUModule;
  MUL MULModule;
  AGU AGUModule;
  BRU BRUModule;
  LQ LQModule;
  SQ SQModule;
  InstructBuffer FQModule;
  ICache ICacheModule;
  DecodeUnit DecodeUnitModule;
  PRF PRFModule;
  DMEM DMEMModule;
  DCache DCacheModule;
  IMEM IMEMModule;
  BPU BPUModule;
  FlushArbiter flushArbiter;
  AluCDB AluCDBArbiterModule;
  LqCDB LqCDBArbiterModule;
  MulCDB MulCDBModule;
  MemArbiter MemArbiterModule;
  DispatchArbiter DispatchArbiterModule;
  IssueArbiter IssueArbiterModule;
  FetchUnit FetchUnitModule;
public:
  CPU(Memory mem);
  void wire();
  void run(bool shuffle = false);
};

#endif // CPU_HPP
