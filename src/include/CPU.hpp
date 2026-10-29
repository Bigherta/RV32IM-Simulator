#pragma once
#include "AGU.hpp"
#include "AXI.hpp"
#include "ALU.hpp"
#include "BPU.hpp"
#include "BRU.hpp"
#include "DCache.hpp"
#include "DIV.hpp"
#include "DMEM.hpp"
#include "Decoder.hpp"
#include "DynamicArbiter.hpp"
#include "FetchUnit.hpp"
#include "ICache.hpp"
#include "IMEM.hpp"
#include "InstructBuffer.hpp"
#include "LQ.hpp"
#include "MUL.hpp"
#include "Memory.hpp"
#include "PRF.hpp"
#include "RAT.hpp"
#include "ROB.hpp"
#include "RS.hpp"
#include "SQ.hpp"
#include "StaticArbiter.hpp"
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
  DCache DCacheModule;
  BPU BPUModule;
  FlushArbiter flushArbiter;
  FetchUnit FetchUnitModule;
  bool initialized = false;
  bool exitCommitted = false;
  RecoveryStep recovery;
  AXIFetch AXIFetchModule;
  AXIData AXIDataModule;
  AXIBus AXIBusModule;
  Slave SlaveModule;

  systemState() = default;
  systemState(const Memory &mem) : SlaveModule(mem) {}
};

class CPU {
private:
  void clock(); // Commit all module updates from the same old snapshot.
  bool frontendCanRun() const;
  systemState CPUstate;
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
  DCache DCacheModule;
  BPU BPUModule;
  FlushArbiter flushArbiter;
  FetchUnit FetchUnitModule;
  bool initialized = false;
  bool exitCommitted = false;
  bool exitRetiring = false;
  RecoveryStep recovery;
  AXIFetch AXIFetchModule;
  AXIData AXIDataModule;
  AXIBus AXIBusModule;
  Slave SlaveModule;
  AXIFetchInput axiFetchInput{};
  AXIDataInput axiDataInput{};
  AXIBusInput axiBusInput{};
  AXIMaster slaveInput{};
  aluCDB cdbOfALU;
  // Source-specific acknowledgements for the single shared CDB1.
  lqCDB cdbOfLQ;
  mulCDB cdbOfMul;
  divCDB cdbOfDiv;
  IssueGroup issuePacket;
  AGUInput aguInput{RSModule, PRFModule};
  ALUInput aluInput{RSModule, PRFModule};
  MULInput mulInput{RSModule, PRFModule};
  DIVInput divInput{RSModule, PRFModule};
  BRUInput bruInput{RSModule, PRFModule};
  BPUInput bpuInput{BRUModule, ROBModule};
  DCacheInput dcacheInput{AXIDataModule};
  DecodeInput decodeInput{FQModule, issuePacket};
  LQInput lqInput{AGUModule, ROBModule, SQModule, issuePacket};
  SQInput sqInput{AGUModule, RSModule, PRFModule, ROBModule, issuePacket};
  RSInput rsInput{issuePacket, PRFModule};
  ROBInput robInput{BRUModule, issuePacket};
  PRFInput prfInput{ROBModule, issuePacket};
  RATInput ratInput{ROBModule, issuePacket};
  FlushArbiterInput flarbInput{BRUModule, ROBModule};
  IssueArbiterInput isarbInput{DecodeUnitModule, ROBModule, RSModule, RATModule,
                               PRFModule,        LQModule,  SQModule};
  SquashInfo squashDetect;
  FetchDecision fetchDecision;
  FetchUnitInput fetchUnitInput;
  IMEMInput imemInput;
  SRAM<NUM_OF_ICACHE_SETS, ICACHE_LINE_BITS, RV32_WORD_BITS>::Input
      icacheDataInput{};
  ICacheInput icacheInput{icacheDataInput};
  FQInput fqInput{ICacheModule, DecodeUnitModule};

public:
  CPU(const Memory &mem);
  void comb();
  void run();
};
