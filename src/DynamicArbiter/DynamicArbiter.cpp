#include "../include/DynamicArbiter.hpp"
#include "../include/CPU.hpp"
#include "../include/util.hpp"
#include <cstdint>
#include <cstring>
#include <stdexcept>

FlushArbiter::FlushArbiter() { std::memset(this, 0, sizeof(*this)); }

void FlushArbiter::receive(const SquashInfo &branch, const SquashInfo &jump,
                           const SquashInfo &accepted, systemState &CPUstate) const {
  auto candidate = [&](int i) -> const SquashInfo & {
    return i < FLUSHARBITER_CAP ? requests[i].requestArgs :
           i == FLUSHARBITER_CAP ? branch : jump;
  };
  auto valid = [&](int i) {
    const bool active = i < FLUSHARBITER_CAP ? requests[i].valid : candidate(i).needSquash;
    return active && (!accepted.needSquash || ROB::isOlder(candidate(i).SquashTag, accepted.SquashTag));
  };
  int count = 0;
  for (int i = 0; i < FLUSHARBITER_CAP + 2; ++i) if (valid(i)) ++count;
  if (count > FLUSHARBITER_CAP) throw std::runtime_error("flush arbiter overload!");
  for (int i = 0; i < FLUSHARBITER_CAP; ++i)
    CPUstate.flushArbiter.requests[i].valid = false;
  for (int i = 0; i < FLUSHARBITER_CAP + 2; ++i) {
    if (!valid(i)) continue;
    int pos = 0;
    for (int j = 0; j < FLUSHARBITER_CAP + 2; ++j) {
      if (!valid(j) || i == j) continue;
      const auto a = candidate(j).SquashTag, b = candidate(i).SquashTag;
      // Original insert order: jump before branch before equal old requests.
      const bool tie = j >= FLUSHARBITER_CAP ? j > i :
                       i < FLUSHARBITER_CAP && j < i;
      if (ROB::isOlder(a, b) || (a == b && tie)) ++pos;
    }
    CPUstate.flushArbiter.requests[pos].valid = true;
    CPUstate.flushArbiter.requests[pos].requestArgs = candidate(i);
  }
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

void FlushArbiter::clear(uint8_t tag, systemState &CPUstate) const {
  for (int i = 0; i < FLUSHARBITER_CAP; ++i) {
    if (requests[i].valid) {
      if (!ROB::isOlder(requests[i].requestArgs.SquashTag, tag)) {
        CPUstate.flushArbiter.requests[i].valid = false;
      }
    }
  }
}

void FlushArbiter::tick(const FlushArbiterInput &input, systemState &CPUstate) {
  SquashInfo BranchSquash, JumpSquash;
  if (input.squashDetect.needSquash)
    clear(input.squashDetect.SquashTag, CPUstate);

  if (!input.BRUModule.isEmpty() &&
      input.ROBModule.matchesTag(input.BRUModule.headRobTag())) {
    uint8_t brRobTag = input.BRUModule.headRobTag();
    const uint32_t pcResult = input.BRUModule.headPCResult();
    const uint32_t pcFrom = input.BRUModule.headPCFrom();
    if (!input.squashDetect.needSquash ||
        (input.squashDetect.needSquash &&
         ROB::isOlder(brRobTag, input.squashDetect.SquashTag))) {
      auto actualPC = pcResult;
      if (actualPC != static_cast<uint32_t>(
                          input.ROBModule.getPredictedPC(robSlot(brRobTag)))) {
        if (debug::enabled(debug::TOPIC_BPMISS))
          debug::print("squash tag=%u pc=%u (from %u)\n", brRobTag, actualPC,
                       pcFrom);
        BranchSquash.needSquash = true;
        BranchSquash.SquashPC = actualPC;
        BranchSquash.SquashTag = brRobTag;
        BranchSquash.CkptId =
            input.ROBModule.getCkptId(robSlot(BranchSquash.SquashTag));
      }
    }
  }

  const auto &cdbOut = input.cdbOut;
  if (cdbOut.valid && input.ROBModule.matchesTag(cdbOut.robTag)) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(cdbOut.robTag, input.squashDetect.SquashTag)) {
      auto isControl = cdbOut.isControl;

      if (!input.ROBModule.isEmpty() &&
          !ROB::isOlder(cdbOut.robTag, input.ROBModule.getHead()) &&
          isControl) {
        const auto pc = static_cast<uint32_t>(cdbOut.value);
        if (pc != input.ROBModule.getPredictedPC(robSlot(cdbOut.robTag))) {
          if (debug::enabled(debug::TOPIC_BPMISS))
            debug::print("squash tag=%u pc=%u (jalr)\n", cdbOut.robTag, pc);
          JumpSquash.needSquash = true;
          JumpSquash.SquashPC = pc;
          JumpSquash.SquashTag = cdbOut.robTag;
          JumpSquash.CkptId =
              input.ROBModule.getCkptId(robSlot(JumpSquash.SquashTag));
        }
      }
    }
  }
  if (BranchSquash.needSquash || JumpSquash.needSquash)
    receive(BranchSquash, JumpSquash, input.squashDetect, CPUstate);
}
