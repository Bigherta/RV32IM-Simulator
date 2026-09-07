#pragma once
#include "../include/common.h"
#include "module.h"
#include "tools.h"
#include <array>
#include <cstdint>

// 64KB 4-way 16B-line data cache: tree-PLRU (3 bit/set) / write-back +
// write-allocate / parked-request FSM / dual-port line-granular DMEM behind it
// (the DCache is DMEM's only client; DMEM never sees sub-word accesses
// anymore).
//
// Hard constraints (mirror main tree):
//  * isBusy == "a request is in flight" -- MemArbiter never issues while busy.
//  * When !isBusy() the DCache must UNCONDITIONALLY accept the decision: the
//    store was already popped from the SQ in this cycle.
//  * Line metadata (valid/dirty/tag/PLRU_bit) lives in DCacheInner, so
//    Module::sync() commits it at the clock edge (Register semantics: a `<=`
//    write becomes visible to the next cycle's reads). The FSM only ever
//    reads metadata written in EARLIER cycles -- probe() runs before every
//    write in READY, and fill (WAIT) and probe (READY) never share a cycle.
//  * datas is hardware SRAM: a plain array written directly by work(),
//    immediately visible. Cacheline::sync() short-circuits the sync walker
//    so it never recurses into the uint8_t bytes (uint8_t is not syncable).
struct DCacheInput {
  // squash guard for load responses (only needSquash/SquashTag are used by
  // the logic; PC/CkptId ride along for plan fidelity)
  Wire<1> squashNeed;
  Wire<7> squashTag;
  Wire<32> squashPC;
  Wire<8> squashCkptId;
  // MemDispatchDecision, split field-by-field (MemArbiter Output wires)
  Wire<1> decisionValid;
  Wire<5> decisionOp; // Operation encoding (Load/Store)
  Wire<32> decisionValue;
  Wire<32> decisionAddr;
  Wire<1> decisionIsSigned;
  Wire<2> decisionNBytes; // 2b encoding: 0->1B, 1->2B, 2->4B (nEnc)
  Wire<7> decisionRobTag;
  Wire<7> decisionMemIndex;
  // DMEM completion observation (_M_old view through the bridge accessors)
  Wire<1> dmemReadBusy;
  Wire<1> dmemWriteBusy;
  Wire<1> dmemReplyReady;
  std::array<Wire<8>, DCACHE_BLOCK_CAP> dmemLineData;
};
struct DCacheOutput {
  Wire<1> busyOut; // -> MemArbiter (busy._M_old); named busyOut so the
                   // bridge method DCache::isBusy() does not shadow it
  // loadResp -> LQ (squash guard inlined: valid && (!needSquash || isOlder))
  Wire<1> loadRespValid;
  Wire<7> loadRespMemIndex;
  Wire<7> loadRespRobTag;
  Wire<32> loadRespValue;
  // forwardRequest -> DMEM dual-port pulses (registered, one-cycle visible)
  Wire<1> reqReadValid;
  Wire<32> reqReadAddr;
  Wire<1> reqWriteValid;
  Wire<32> reqWriteAddr;
  std::array<Wire<8>, DCACHE_BLOCK_CAP> reqWriteLineData;
};
// Cacheline/CacheSet must precede DCacheInner (the Inner embeds the array).
// Register::sync() is private (friend Visitor only), so the custom sync()
// methods route through the public dark::Visitor::sync; defining sync() on
// Cacheline also short-circuits the aggregate walk past the plain datas
// bytes (sync_member's is_syncable branch fires before the tuplify walk).
struct Cacheline {
  Register<1> valid;
  Register<1> dirty;
  std::array<uint8_t, DCACHE_BLOCK_CAP> datas{}; // plain SRAM: never synced
  Register<18> tag;
  void sync() {
    dark::Visitor::sync(valid);
    dark::Visitor::sync(dirty);
    dark::Visitor::sync(tag);
  }
};
struct CacheSet {
  std::array<Cacheline, NUM_OF_WAYS> lines;
  Register<3> PLRU_bit;
  void sync() {
    for (auto &l : lines)
      dark::Visitor::sync(l);
    dark::Visitor::sync(PLRU_bit);
  }
};
struct DCacheInner {
  Register<1> busy;
  Register<1> phase; // READY=0, WAIT=1
  // parked decision (the miss that waits for its fill); "a request is
  // parked" == phase==WAIT, no separate valid bit is needed
  Register<5> parkOp;
  Register<32> parkValue;
  Register<32> parkAddr;
  Register<1> parkIsSigned;
  Register<2> parkNBytes; // nEnc encoding (see decisionNBytes)
  Register<7> parkRobTag;
  Register<7> parkMemIndex;
  Register<2> parkTargetWay; // allocateWay result, 0..3
  // self-answer / fill-serve load response (one-cycle pulse)
  Register<1> lbValid; // loadBuffer (Inner side renamed: Output uses loadResp*)
  Register<7> lbMemIndex;
  Register<7> lbRobTag;
  Register<32> lbValue;
  // forwarded request pulses (cleared each cycle, claimed by DMEM next)
  Register<1> rqReadValid; // Output side keeps the reqRead* names
  Register<32> rqReadAddr;
  Register<1> rqWriteValid;
  Register<32> rqWriteAddr;
  std::array<Register<8>, DCACHE_BLOCK_CAP> rqWriteLineData;
  // Line array inside Inner so Module::sync() commits the metadata
  // registers every cycle (路线 A); datas inside each line stays plain.
  std::array<CacheSet, NUM_OF_SETS> cacheSets;
};
struct DCache : dark::Module<DCacheInput, DCacheOutput, DCacheInner> {
  DCache() { wire_output(); }
  void work() override;
  bool isBusy() const { return static_cast<bool>(busy); }
  // Hit/miss/writeback accumulators (VERBOSE=dcache summary). Software
  // counters: plain members, never visited by Module::sync().
  uint64_t statHits = 0;
  uint64_t statMisses = 0;
  uint64_t statWritebacks = 0;

private:
  void wire_output();
  // combinational helpers (pure reads over cacheSets / _M_old registers)
  struct Probe {
    bool hit;
    uint32_t way;
  };
  Probe probe(uint32_t addr) const;
  uint32_t allocateWay(uint32_t set_index) const; // victim assert in _DEBUG
  static int decodeNBytes(uint32_t enc);
  // sign-extended sub-word load (mask branch identical to DMEM::load_n_bytes)
  static int32_t extractValue(const uint8_t *datas, int off, int n,
                              bool isSigned);
};
