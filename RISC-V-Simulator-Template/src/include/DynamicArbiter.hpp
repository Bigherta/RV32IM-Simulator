#pragma once
// DynamicArbiter: the stateful arbiter family (flip-flops in RTL).
// FlushArbiter is its only member: it owns the squash-request queue
// (FlushRequest registers held in the Module Inner, FLUSHARBITER_CAP deep)
// and runs a real multi-stage work(). The stateless combinational arbiters
// live in StaticArbiter.hpp -- the stateful/stateless split is the hardware
// boundary (registered queue vs pure always_comb), mirrored by this file
// split.
#include "common.h"
#include "module.h"
#include <array>
#include <cstdint>
struct FlushRequest {
  // NOTE: no per-slot needSquash -- valid==1 implies needSquash==1 by
  // construction (all three inserts only fire when the local SquashInfo
  // has needSquash==true), so the broadcast Output derives it as
  // (selectOldest() != nullptr).
  Register<7> SquashTag;
  Register<32> SquashPC;
  Register<6> CkptId;
  Register<1> valid;
};

struct FlushArbiterInputSquash {
  Wire<1> needSquash;
  Wire<7> SquashTag;
};
struct FlushArbiterInputCDB {
  Wire<1> cdbValid;
  Wire<32> cdbValue;
  Wire<7> cdbRobTag;
  Wire<1> cdbIsControl;
};
struct FlushArbiterInputBRU {
  Wire<1> isBRUEmpty;
  Wire<7> bruHeadRobTag;
  Wire<32> bruHeadPCResult;
  Wire<32> bruHeadPCFrom;
};
struct FlushArbiterInputROB {
  Wire<1> isROBEmpty;
  Wire<7> robHeadTag;
  std::array<Wire<32>, ROB_CAP> robPredictPC;
  std::array<Wire<32>, ROB_CAP> robPC; // true fetch PC (load entries included)
  std::array<Wire<6>, ROB_CAP> robCkptId;
};
struct FlushArbiterInputLQ {
  Wire<8> lqHead;
  std::array<Wire<1>, LQ_CAP> lqActive;
  std::array<Wire<1>, LQ_CAP> lqAddressReady;
  std::array<Wire<7>, LQ_CAP> lqRobTags;
  std::array<Wire<32>, LQ_CAP> lqAddress;
  std::array<Wire<2>, LQ_CAP> lqValueState;
};
struct FlushArbiterInputAGU{
  Wire<1> isAGUEmpty;
  Wire<32> aguHeadValue;
  Wire<7> aguHeadMemIndex;
  Wire<7> aguHeadRobTag;
};
// FlushArbiter owns its queue and the whole squash flow: stage 1 consumes
// the accepted squash (clear), stage 2 detects BRU branch mispredicts,
// stage 3 detects CDB JALR mispredicts, stage 4 detects MDP load violations
// (store address resolves against younger executed loads) -- all reads from
// the committed Input Wire views (BRUModule head, aluCDB output,
// ROBModule, AGUModule head store, LQModule/SQModule), all writes to its own
// queue (receive).
struct FlushArbiterInput {
  FlushArbiterInputBRU bru;
  FlushArbiterInputCDB cdb;
  FlushArbiterInputLQ lq;
  FlushArbiterInputROB rob;
  FlushArbiterInputAGU agu;
  FlushArbiterInputSquash squash;
};
struct FlushArbiterInner {
  std::array<FlushRequest, FLUSHARBITER_CAP> requests;
};
// Squash broadcast view over the committed request queue: every consumer
// reads these four Output Wires directly (the comb-built SquashInfo CPU
// member and the arbitResult() bridge were retired with the stage-8 bus
// migration; SquashInfo survives only as FlushArbiter's internal
// plain-queue insert payload).
struct FlushArbiterOutput {
  Wire<1> needSquash;
  Wire<7> SquashTag;
  Wire<32> SquashPC;
  Wire<6> CkptId;
};
struct FlushArbiter : dark::Module<FlushArbiterInput, FlushArbiterOutput,
                                   FlushArbiterInner>{
public:
  FlushArbiter() { wire_output(); }
  void work() override;

private:
  void wire_output();
  // Oldest valid request (verbatim from the former inline scan in
  // arbitResult): first valid slot wins, strictly-older tags replace it.
  const FlushRequest *selectOldest() const;
};
