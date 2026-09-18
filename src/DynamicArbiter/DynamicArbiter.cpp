#include "../include/DynamicArbiter.hpp"
#include "../include/CPU.hpp"
#include "../include/util.hpp"
#include <cstdint>
#include <cstring>
#include <stdexcept>

FlushArbiter::FlushArbiter() { std::memset(this, 0, sizeof(*this)); }

void FlushArbiter::receive(SquashInfo request) {
  int w = 0;
  for (int r = 0; r < FLUSHARBITER_CAP; ++r) {
    if (requests[r].valid) {
      if (r != w) {
        requests[w] = requests[r];
        requests[r].valid = false;
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
    if (ROB::isYounger(request.SquashTag, requests[i].requestArgs.SquashTag))
      ++pos;
    else
      scanning = false;
  }
  for (int i = FLUSHARBITER_CAP - 1; i > 0; --i)
    if (i > pos)
      requests[i] = requests[i - 1];
  requests[pos].valid = true;
  requests[pos].requestArgs = request;
}

SquashInfo FlushArbiter::arbitResult() const {
  SquashInfo result{};
  bool found = false;
  for (int i = 0; i < FLUSHARBITER_CAP; ++i) {
    if (requests[i].valid) {
      if (!found ||
          ROB::isOlder(requests[i].requestArgs.SquashTag, result.SquashTag)) {
        result = requests[i].requestArgs;
        found = true;
      }
    }
  }
  return result;
}

void FlushArbiter::clear(uint8_t tag) {
  for (int i = 0; i < FLUSHARBITER_CAP; ++i) {
    if (requests[i].valid) {
      if (!ROB::isOlder(requests[i].requestArgs.SquashTag, tag)) {
        requests[i].valid = false;
      }
    }
  }
}

void FlushArbiter::tick(const FlushArbiterInput &input, systemState &CPUstate) {
  if (input.squashDetect.needSquash)
    CPUstate.flushArbiter.clear(input.squashDetect.SquashTag);

  if (!input.BRUModule.isEmpty()) {
    SquashInfo BranchSquash;
    uint8_t brRobTag = input.BRUModule.headRobTag();
    int pcResult = input.BRUModule.headPCResult();
    int pcFrom = input.BRUModule.headPCFrom();
    if (!input.squashDetect.needSquash ||
        (input.squashDetect.needSquash &&
         ROB::isOlder(brRobTag, input.squashDetect.SquashTag))) {
      auto actualPC = pcResult;
      if (actualPC != input.ROBModule.getPredictedPC(((brRobTag) & 0x3F))) {
        if (debug::enabled(debug::TOPIC_BPMISS))
          debug::print("squash tag=%u pc=%u (from %u)\n", brRobTag, actualPC,
                       pcFrom);
        BranchSquash.needSquash = true;
        BranchSquash.SquashPC = actualPC;
        BranchSquash.SquashTag = brRobTag;
        BranchSquash.CkptId =
            input.ROBModule.getCkptId(BranchSquash.SquashTag & 0x3F);
      }
    }
    if (BranchSquash.needSquash)
      CPUstate.flushArbiter.receive(BranchSquash);
  }

  const auto &cdbOut = input.cdbOut;
  if (cdbOut.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(cdbOut.robTag, input.squashDetect.SquashTag)) {
      auto isControl = cdbOut.isControl;

      if (!input.ROBModule.isEmpty() &&
          !ROB::isOlder(cdbOut.robTag, input.ROBModule.getHead()) &&
          isControl) {
        SquashInfo JumpSquash;
        const auto pc = static_cast<uint32_t>(cdbOut.value);
        if (pc != input.ROBModule.getPredictedPC(((cdbOut.robTag) & 0x3F))) {
          if (debug::enabled(debug::TOPIC_BPMISS))
            debug::print("squash tag=%u pc=%u (jalr)\n", cdbOut.robTag, pc);
          JumpSquash.needSquash = true;
          JumpSquash.SquashPC = pc;
          JumpSquash.SquashTag = cdbOut.robTag;
          JumpSquash.CkptId =
              input.ROBModule.getCkptId(JumpSquash.SquashTag & 0x3F);
        }
        if (JumpSquash.needSquash)
          CPUstate.flushArbiter.receive(JumpSquash);
      }
    }
  }

  if (!input.AGUModule.isEmpty() &&
      isStoreMem(input.AGUModule.headMemIndex())) {
    auto aguRobTag = input.AGUModule.headRobTag();
    if (!input.squashDetect.needSquash ||
        (input.squashDetect.needSquash &&
         ROB::isOlder(aguRobTag, input.squashDetect.SquashTag))) {
      auto storeAddr = static_cast<uint32_t>(input.AGUModule.headValue());
      auto lqHead = input.LQModule.getHead();
      bool violationHandled = false;
      for (int k = 0; k < LQ_CAP; ++k) {
        uint8_t i = (lqHead + k) & 0x0F;
        if (violationHandled)
          continue;
        if (!input.LQModule.isActive(i))
          continue;
        if (input.LQModule.isAddressReady(i) &&
            input.LQModule.getAddress(i) == storeAddr &&
            (input.LQModule.getValueState(i) == ValueState::READY ||
             input.LQModule.getValueState(i) == ValueState::FETCHING) &&
            ROB::isYounger(input.LQModule.getRobTag(i), aguRobTag)) {
          auto violTag = input.LQModule.getRobTag(i);
          if ((!input.squashDetect.needSquash ||
               ROB::isOlder(violTag, input.squashDetect.SquashTag)) &&
              !input.ROBModule.isEmpty() &&
              !ROB::isOlder(violTag, input.ROBModule.getHead())) {
            SquashInfo viol;
            viol.needSquash = true;
            viol.SquashTag = violTag;
            viol.SquashPC = input.ROBModule.getPC(viol.SquashTag & 0x3F);
            viol.CkptId = input.ROBModule.getCkptId(viol.SquashTag & 0x3F);
            CPUstate.flushArbiter.receive(viol);
            violationHandled = true;
          }
        }
      }
    }
  }
}
