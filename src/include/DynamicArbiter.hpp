#pragma once
// DynamicArbiter: the stateful arbiter family (flip-flops in RTL). FlushArbiter
// is its only member -- it owns the squash-request queue (requests[] holds
// across cycles) and runs a real tick(). The stateless combinational arbiters
// live in StaticArbiter.hpp; the stateful/stateless split is the hardware
// boundary (registered queue vs pure always_comb), mirrored by this file split.
// NOTE: this tree is still the comb()/tick()/memcpy reference implementation --
// only the file layout follows RISC-V-Simulator-Template/, the classes here are
// plain C++ (no Register/Wire/dark::Module).
#include "AGU.hpp"
#include "BRU.hpp"
#include "LQ.hpp"
#include "ROB.hpp"
#include "common.hpp"
#include <cstdint>
struct FlushRequest {
  SquashInfo requestArgs;
  bool valid;
};

struct systemState;
class ROB;

// FlushArbiter owns its queue and the whole squash flow: stage 1 consumes
// the accepted squash (clear), stage 2 detects BRU branch mispredicts,
// stage 3 detects CDB JALR mispredicts, stage 4 detects MDP load violations
// (store address resolves against younger executed loads) -- all reads from
// the snapshots (BRUModule head, cdbOut, ROBModule, AGUModule head store,
// LQModule), all writes to its own queue (receive).
struct FlushArbiterInput {
  const BRU &BRUModule;
  const ROB &ROBModule;
  const AGU &AGUModule;
  const LQ &LQModule;
  aluCDB cdbOut;
  SquashInfo squashDetect;
  FlushArbiterInput(const BRU &bru, const ROB &rob, const AGU &agu,
                    const LQ &lq)
      : BRUModule(bru), ROBModule(rob), AGUModule(agu), LQModule(lq) {}
};

class FlushArbiter {
private:
  FlushRequest requests[FLUSHARBITER_CAP];
  void receive(SquashInfo request);
  void clear(uint8_t tag);

public:
  FlushArbiter();
  SquashInfo arbitResult() const;
  void tick(const FlushArbiterInput &, systemState &);
};
