#pragma once
#include "AGU.hpp"
#include "ALU.hpp"
#include "DynamicArbiter.hpp"
#include "StaticArbiter.hpp"
#include "BRU.hpp"
#include "MUL.hpp"
#include "DIV.hpp"
#include "BPU.hpp"
#include "DCache.hpp"
#include "DMEM.hpp"
#include "Decoder.hpp"
#include "InstructBuffer.hpp"
#include "FetchUnit.hpp"
#include "ICache.hpp"
#include "IMEM.hpp"
#include "LQ.hpp"
#include "SQ.hpp"
#include "Memory.hpp"
#include "PRF.hpp"
#include "RAT.hpp"
#include "ROB.hpp"
#include "RS.hpp"
#include "common.hpp"
#include <cstring>

struct systemState {
  RSUnit RSModule;
  RAT RATModule;
  ROB ROBModule;
  ALU ALUModule;
  AGU AGUModule;
  MUL MULModule;
  DIV DIVModule;
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
  FetchUnit FetchUnitModule;

  systemState() = default;
  systemState(Memory mem)
      : DMEMModule(mem), IMEMModule(mem) {}
};

class CPU {
private:
  systemState CPUstate;
  IMEM InstructMem;
  RSUnit RSModule;
  RAT RATModule;
  ROB ROBModule;
  ALU ALUModule;
  AGU AGUModule;
  MUL MULModule;
  DIV DIVModule;
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
  FetchUnit FetchUnitModule;
  aluCDB cdbOfALU;
  lqCDB cdbOfLQ;
  mulCDB cdbOfMul; // MUL 专用结果总线（cdbOfALU/cdbOfLQ/cdbOfMul 三路，各源独立、无跨单元仲裁）
  divCDB cdbOfDiv; // fourth independent result bus (ALU/LQ/MUL/DIV)
  uint64_t statAluOnly = 0;   // cycles with only an ALU CDB candidate
  uint64_t statLqOnly = 0;    // cycles with only an LQ CDB candidate
  uint64_t statBoth = 0;      // cycles both valid (dual-CDB parallel grant)
  uint64_t statLqWins = 0;    // both valid, LQ older (single-CDB would grant LQ)
  uint64_t statAluWins = 0;   // both valid, ALU older (single-CDB would grant ALU)
  IssuePacket issuePacket;
  AGUInput aguInput{RSModule, PRFModule};
  ALUInput aluInput{RSModule, PRFModule};
  MULInput mulInput{RSModule, PRFModule};
  DIVInput divInput{RSModule, PRFModule};
  BRUInput bruInput{ROBModule, RSModule, PRFModule};
  BPUInput bpuInput{BRUModule, ROBModule};
  DMEMInput dmemInput{};
  DCacheInput dcacheInput{DMEMModule};
  DecodeInput decodeInput{FQModule, issuePacket};
  LQInput lqInput{AGUModule, RSModule, ROBModule, DMEMModule, SQModule,
                   issuePacket};
  SQInput sqInput{AGUModule, RSModule, PRFModule, ROBModule, DMEMModule,
                   LQModule, issuePacket};
  RSInput rsInput{issuePacket, PRFModule};
  ROBInput robInput{BRUModule, SQModule, issuePacket};
  PRFInput prfInput{ROBModule, issuePacket};
  RATInput ratInput{ROBModule, issuePacket};
  FlushArbiterInput flarbInput{BRUModule, ROBModule, AGUModule, LQModule};
  IssueArbiterInput isarbInput{DecodeUnitModule, ROBModule, RSModule,
                                RATModule,        PRFModule, LQModule,
                                SQModule};
  SquashInfo squashDetect;
  FetchDecision fetchDecision;
  FetchUnitInput fetchUnitInput;
  IMEMInput imemInput;
  ICacheInput icacheInput{};
  FQInput fqInput{ICacheModule, DecodeUnitModule};
public:
  CPU(Memory mem);
  void comb();
  void run();
};
