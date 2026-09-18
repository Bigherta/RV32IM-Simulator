#include "../include/DynamicArbiter.hpp"
#include "../include/ROB.hpp"
#include "../include/util.hpp"
#include <cstdint>
#include <stdexcept>

namespace {
// Plain (non-Register) snapshot of one flush-request slot, used for the
// single-cycle composition: load committed world -> clear -> 3 ordered
// inserts -> single write-back (Register single-write discipline).
struct FlushReqPlain {
  bool valid = false;
  uint32_t squashTag = 0;
  uint32_t squashPC = 0;
  uint32_t ckptId = 0;
};

// Exact port of main-tree FlushArbiter::receive: compact valid entries,
// then age-ordered insert (oldest first). Plain-to-plain so consecutive
// inserts in one work() see each other's result (Register reads would see
// the stale _M_old world).
void insertPlain(FlushReqPlain cur[FLUSHARBITER_CAP],
                 const SquashInfo &request) {
  int w = 0;
  for (int r = 0; r < FLUSHARBITER_CAP; ++r) {
    if (cur[r].valid) {
      if (r != w) {
        cur[w] = cur[r];
        cur[r].valid = false;
      }
      ++w;
    }
  }
  if (w == FLUSHARBITER_CAP)
    throw std::runtime_error("flush arbiter overload!");
  int pos = 0;
  bool scanning = true;
  for (int i = 0; i < FLUSHARBITER_CAP; ++i) {
    if (!scanning || i >= w)
      continue;
    if (ROB::isYounger(request.SquashTag, cur[i].squashTag))
      ++pos;
    else
      scanning = false;
  }
  for (int i = FLUSHARBITER_CAP - 1; i > 0; --i)
    if (i > pos)
      cur[i] = cur[i - 1];
  cur[pos].valid = true;
  // request.needSquash is true at all three call sites (each gated on the
  // local SquashInfo's needSquash); valid==1 implies it, no slot storage.
  cur[pos].squashTag = request.SquashTag;
  cur[pos].squashPC = request.SquashPC;
  cur[pos].ckptId = request.CkptId;
}
} // namespace

const FlushRequest *FlushArbiter::selectOldest() const {
  const FlushRequest *best = nullptr;
  for (int i = 0; i < FLUSHARBITER_CAP; ++i) {
    if (!static_cast<bool>(requests[i].valid))
      continue;
    if (best == nullptr ||
        ROB::isOlder(static_cast<uint32_t>(requests[i].SquashTag),
                     static_cast<uint32_t>(best->SquashTag)))
      best = &requests[i];
  }
  return best;
}

void FlushArbiter::wire_output() {
  needSquash = [this]() -> uint32_t {
    // valid==1 implies needSquash==1 (see FlushRequest note): a hit means 1.
    return selectOldest() ? 1u : 0u;
  };
  SquashTag = [this]() -> uint32_t {
    const FlushRequest *win = selectOldest();
    return win ? static_cast<uint32_t>(win->SquashTag) : 0u;
  };
  SquashPC = [this]() -> uint32_t {
    const FlushRequest *win = selectOldest();
    return win ? static_cast<uint32_t>(win->SquashPC) : 0u;
  };
  CkptId = [this]() -> uint32_t {
    const FlushRequest *win = selectOldest();
    return win ? static_cast<uint32_t>(win->CkptId) : 0u;
  };
}

void FlushArbiter::work() {
  // Stage 0: snapshot the committed world (Register _M_old -> plain).
  FlushReqPlain cur[FLUSHARBITER_CAP];
  for (int i = 0; i < FLUSHARBITER_CAP; ++i) {
    cur[i].valid = static_cast<bool>(requests[i].valid);
    cur[i].squashTag = static_cast<uint32_t>(requests[i].SquashTag);
    cur[i].squashPC = static_cast<uint32_t>(requests[i].SquashPC);
    cur[i].ckptId = static_cast<uint32_t>(requests[i].CkptId);
  }

  // Stage 1: clear (plain domain) -- main-tree tick step 1.
  if (static_cast<bool>(squash.needSquash)) {
    uint32_t tag = static_cast<uint32_t>(squash.SquashTag);
    for (int i = 0; i < FLUSHARBITER_CAP; ++i)
      if (cur[i].valid && !ROB::isOlder(cur[i].squashTag, tag))
        cur[i].valid = false;
  }

  // Stage 2-4: detection stages -> ordered inserts.
  if (debug::enabled(debug::TOPIC_BRANCH))
    debug::print(
        "F2 e=%d tag=%x res=%x pred=%x sqn=%d\n",
        static_cast<uint32_t>(bru.isBRUEmpty),
        static_cast<uint32_t>(bru.bruHeadRobTag),
        static_cast<uint32_t>(bru.bruHeadPCResult),
        static_cast<uint32_t>(
            rob.robPredictPC[static_cast<uint32_t>(bru.bruHeadRobTag) & 0x3F]),
        static_cast<bool>(squash.needSquash) ? 1 : 0);
  if (bru.isBRUEmpty == 0) {
    SquashInfo BranchSquash;
    auto brRobTag = static_cast<uint32_t>(bru.bruHeadRobTag);
    auto pcResult = static_cast<uint32_t>(bru.bruHeadPCResult);
    auto pcFrom = static_cast<uint32_t>(bru.bruHeadPCFrom);
    if (squash.needSquash == 0 ||
        (squash.needSquash &&
         ROB::isOlder(brRobTag, static_cast<uint32_t>(squash.SquashTag)))) {
      auto actualPC = pcResult;
      auto predPC = static_cast<uint32_t>(rob.robPredictPC[brRobTag & 0x3F]);
      if (actualPC != predPC) {
        if (debug::enabled(debug::TOPIC_BRANCH))
          debug::print("squash tag=%u pc=%u (from %u)\n", brRobTag, actualPC,
                       pcFrom);
        BranchSquash.needSquash = true;
        BranchSquash.SquashPC = actualPC;
        BranchSquash.SquashTag = brRobTag;
        BranchSquash.CkptId = static_cast<uint32_t>(
            rob.robCkptId[static_cast<uint32_t>(brRobTag & 0x3F)]);
      }
    }
    if (BranchSquash.needSquash)
      insertPlain(cur, BranchSquash);
  }

  if (cdb.cdbValid) {
    if (squash.needSquash == 0 ||
        ROB::isOlder(static_cast<uint32_t>(cdb.cdbRobTag),
                     static_cast<uint32_t>(squash.SquashTag))) {
      auto isControl = static_cast<bool>(cdb.cdbIsControl);

      if (rob.isROBEmpty == 0 &&
          !ROB::isOlder(static_cast<uint32_t>(cdb.cdbRobTag),
                        static_cast<uint32_t>(rob.robHeadTag)) &&
          isControl) {
        SquashInfo JumpSquash;
        const auto pc = static_cast<uint32_t>(cdb.cdbValue);
        if (pc !=
            rob.robPredictPC[static_cast<uint32_t>(cdb.cdbRobTag & 0x3F)]) {
          if (debug::enabled(debug::TOPIC_BRANCH))
            debug::print("squash tag=%u pc=%u (jalr)\n",
                         static_cast<uint32_t>(cdb.cdbRobTag), pc);
          JumpSquash.needSquash = true;
          JumpSquash.SquashPC = pc;
          JumpSquash.SquashTag = static_cast<uint32_t>(cdb.cdbRobTag);
          JumpSquash.CkptId = static_cast<uint32_t>(
              rob.robCkptId[static_cast<uint32_t>(cdb.cdbRobTag & 0x3F)]);
        }
        if (JumpSquash.needSquash)
          insertPlain(cur, JumpSquash);
      }
    }
  }

  if (agu.isAGUEmpty == 0 &&
      isStoreMem(static_cast<uint32_t>(agu.aguHeadMemIndex))) {
    auto aguRobTag = static_cast<uint32_t>(agu.aguHeadRobTag);
    if (squash.needSquash == 0 ||
        (squash.needSquash &&
         ROB::isOlder(aguRobTag, static_cast<uint32_t>(squash.SquashTag)))) {
      auto storeAddr = static_cast<uint32_t>(agu.aguHeadValue);
      auto lqHead = static_cast<uint32_t>(lq.lqHead);
      bool violationHandled = false;
      for (int k = 0; k < LQ_CAP; ++k) {
        uint8_t i = (lqHead + k) & 0x0F;
        if (violationHandled)
          continue;
        if (lq.lqActive[i] == 0)
          continue;
        if (lq.lqAddressReady[i] &&
            static_cast<uint32_t>(lq.lqAddress[i]) == storeAddr &&
            (lq.lqValueState[i] == static_cast<uint32_t>(ValueState::READY) ||
             lq.lqValueState[i] ==
                 static_cast<uint32_t>(ValueState::FETCHING)) &&
            ROB::isYounger(static_cast<uint32_t>(lq.lqRobTags[i]), aguRobTag)) {
          auto violTag = static_cast<uint32_t>(lq.lqRobTags[i]);
          if ((squash.needSquash == 0 ||
               ROB::isOlder(violTag,
                            static_cast<uint32_t>(squash.SquashTag))) &&
              rob.isROBEmpty == 0 &&
              !ROB::isOlder(violTag, static_cast<uint32_t>(rob.robHeadTag))) {
            SquashInfo viol;
            viol.needSquash = true;
            viol.SquashTag = violTag;
            // The redirect target is the VIOLATING LOAD's own PC (ROB::getPC),
            // NOT its predictedPC: load ROB entries carry predictedPC == 0
            // (only INT/BR/UJ issuers assign it), so robPredictPC would
            // squash the machine to address 0 and restart the whole program.
            viol.SquashPC = static_cast<uint32_t>(rob.robPC[violTag & 0x3F]);
            viol.CkptId = static_cast<uint32_t>(rob.robCkptId[violTag & 0x3F]);
            insertPlain(cur, viol);
            violationHandled = true;
          }
        }
      }
    }
  }

  // Stage 5: single write-back, 4 fields x CAP slots, each Register once.
  for (int i = 0; i < FLUSHARBITER_CAP; ++i) {
    requests[i].valid <= cur[i].valid;
    requests[i].SquashTag <= cur[i].squashTag;
    requests[i].SquashPC <= cur[i].squashPC;
    requests[i].CkptId <= cur[i].ckptId;
  }
}
