#include "../include/SQ.hpp"
#include "../include/CPU.hpp"
#include "../include/ROB.hpp"
#include <cstdint>
#include <stdexcept>

bool SQ::isEmpty() const { return tail == head; }

bool SQ::isFull() const { return tail == (head ^ SQ_CAP); }

bool SQ::isActive(uint8_t index) const {
  if (head == tail)
    return false;
  return index < SQ_CAP && ((index - head) & SQ_MASK) <
         ((tail - head) & SQ_SEQ_MASK);
}

void SQ::pop(systemState &CPUstate) const { CPUstate.SQModule.head = (head + 1) & SQ_SEQ_MASK; }

void SQ::pushStore(RobTag robTag, int n_bytes, systemState &CPUstate) const {
  const auto slot = tail & SQ_MASK;
  CPUstate.SQModule.SQqueue[slot] = {};
  CPUstate.SQModule.SQqueue[slot].robTag = robTag;
  CPUstate.SQModule.SQqueue[slot].n_bytes = n_bytes;
  CPUstate.SQModule.SQqueue[slot].isAddressReady = false;
  CPUstate.SQModule.SQqueue[slot].isValueReady = false;
  CPUstate.SQModule.SQqueue[slot].isCommitted = false;
  CPUstate.SQModule.tail = (tail + 1) & SQ_SEQ_MASK;
}

uint8_t SQ::getHead() const { return head & SQ_MASK; }
uint8_t SQ::getTail() const { return tail; }

void SQ::flush(uint8_t tailSnapshot, systemState &CPUstate) const { CPUstate.SQModule.tail = tailSnapshot; }

void SQ::writeAddress(uint32_t address, int index, systemState &CPUstate) const {
  CPUstate.SQModule.SQqueue[index].address = address;
  CPUstate.SQModule.SQqueue[index].isAddressReady = true;
}

void SQ::writeValue(int32_t value, int index, systemState &CPUstate) const {
  CPUstate.SQModule.SQqueue[index].value = value;
  CPUstate.SQModule.SQqueue[index].isValueReady = true;
}
void SQ::setCommitted(int index, systemState &CPUstate) const {
  CPUstate.SQModule.SQqueue[index].isCommitted = true;
}

auto SQ::getAddress(int index) const -> uint32_t {
  if (SQqueue[index].isAddressReady)
    return SQqueue[index].address;
  throw std::runtime_error("Address is not ready!");
}

auto SQ::getValue(int index) const -> int32_t {
  if (SQqueue[index].isValueReady)
    return SQqueue[index].value;
  throw std::runtime_error("Value is not ready!");
}

auto SQ::headRobTag() const -> uint8_t { return SQqueue[head & SQ_MASK].robTag; }

auto SQ::getRobTag(int index) const -> uint8_t { return SQqueue[index].robTag; }

auto SQ::getNBytes(int index) const -> int { return SQqueue[index].n_bytes; }

auto SQ::planDataForward(int index, int32_t value) const -> StoreNotify {
  StoreNotify notify{};
  if (!SQqueue[index].isAddressReady)
    return notify;
  notify.storeTag = SQqueue[index].robTag;
  notify.addr = SQqueue[index].address;
  notify.value = value;
  uint8_t knownSameAddressOldestTag = SQqueue[index].robTag;
  uint8_t unknownOldestTag = SQqueue[index].robTag;
  bool FoundKnownSameAddressOldest = false;
  bool FoundUnknownOldest = false;
  for (int k = 1; k <= SQ_CAP; ++k) {
    uint8_t i = (index + k) & SQ_MASK;
    if (i == index || !isActive(i))
      continue;
    if (((i - head) & SQ_MASK) <= ((index - head) & SQ_MASK))
      continue;
    if (SQqueue[i].address == SQqueue[index].address &&
        SQqueue[i].isAddressReady && !FoundKnownSameAddressOldest) {
      knownSameAddressOldestTag = SQqueue[i].robTag;
      FoundKnownSameAddressOldest = true;
    } else if (!SQqueue[i].isAddressReady && !FoundUnknownOldest) {
      unknownOldestTag = SQqueue[i].robTag;
      FoundUnknownOldest = true;
    }
  }
  notify.foundKnownSame = FoundKnownSameAddressOldest;
  notify.knownSameAddressOldestTag = knownSameAddressOldestTag;
  notify.foundUnknown = FoundUnknownOldest;
  notify.unknownOldestTag = unknownOldestTag;
  notify.valid = true;
  return notify;
}

auto SQ::planAddressForward(int index, uint32_t address) const -> StoreNotify {
  StoreNotify notify{};
  if (!SQqueue[index].isValueReady)
    return notify;
  notify.storeTag = SQqueue[index].robTag;
  notify.addr = address;
  notify.value = SQqueue[index].value;
  uint8_t knownSameAddressOldestTag = SQqueue[index].robTag;
  uint8_t unknownOldestTag = SQqueue[index].robTag;
  bool FoundKnownSameAddressOldest = false;
  bool FoundUnknownOldest = false;
  for (int k = 1; k <= SQ_CAP; ++k) {
    uint8_t i = (index + k) & SQ_MASK;
    if (i == index || !isActive(i))
      continue;
    if (((i - head) & SQ_MASK) <= ((index - head) & SQ_MASK))
      continue;
    if (SQqueue[i].address == address && SQqueue[i].isAddressReady &&
        !FoundKnownSameAddressOldest) {
      knownSameAddressOldestTag = SQqueue[i].robTag;
      FoundKnownSameAddressOldest = true;
    } else if (!SQqueue[i].isAddressReady && !FoundUnknownOldest) {
      unknownOldestTag = SQqueue[i].robTag;
      FoundUnknownOldest = true;
    }
  }
  notify.foundKnownSame = FoundKnownSameAddressOldest;
  notify.knownSameAddressOldestTag = knownSameAddressOldestTag;
  notify.foundUnknown = FoundUnknownOldest;
  notify.unknownOldestTag = unknownOldestTag;
  notify.valid = true;
  return notify;
}

auto SQ::replyToLoadRequest(uint32_t addr,
                            uint8_t loadTag) const -> StoreResponse {
  int youngestSameAddrOrder = -1;
  int youngestUnknownOrder = -1;
  int forwardValue = 0;
  bool FoundSameAddr = false;
  bool SameAddrValueReady = false;
  bool reachedYoungerStore = false;
  for (int k = 0; k < SQ_CAP; k++) {
    int index = (head + k) & SQ_MASK;
    if (!isActive(index))
      continue;
    if (!SQqueue[index].isCommitted &&
        ROB::isYounger(SQqueue[index].robTag, loadTag)) {
      reachedYoungerStore = true;
    }
    if (reachedYoungerStore)
      continue;
    if (!SQqueue[index].isAddressReady) {
      youngestUnknownOrder = k;
    } else if (SQqueue[index].address == addr) {
      youngestSameAddrOrder = k;
      FoundSameAddr = true;
      SameAddrValueReady = SQqueue[index].isValueReady;
      if (SQqueue[index].isValueReady)
        forwardValue = SQqueue[index].value;
    }
  }
  StoreResponse reply{};
  reply.valid = SameAddrValueReady && FoundSameAddr &&
                youngestSameAddrOrder > youngestUnknownOrder;
  reply.value = forwardValue;
  return reply;
}

bool SQ::canDispatchLoad(uint32_t addr, RobTag loadTag) const {
  bool hasSameAddressStore = false;
  bool hasUnknownAddressStore = false;
  for (int k = 0; k < SQ_CAP; k++) {
    if (hasSameAddressStore)
      continue;
    uint8_t cur = (head + k) & SQ_MASK;
    if (!isActive(cur))
      continue;
    if (SQqueue[cur].isCommitted)
      continue;
    if (!ROB::isOlder(SQqueue[cur].robTag, loadTag))
      continue;
    if (SQqueue[cur].isAddressReady && SQqueue[cur].address == addr)
      hasSameAddressStore = true;
    if (!SQqueue[cur].isAddressReady)
      hasUnknownAddressStore = true;
  }
  return !hasSameAddressStore && !hasUnknownAddressStore;
}

bool SQ::isReadyToCommit(int index) const {
  return SQqueue[index].isAddressReady && SQqueue[index].isValueReady;
}

void SQ::tick(const SQInput &input, systemState &CPUstate) {
  // issue apply: push the pre-built store entry
  const auto &p = input.issuePacket;
  if (p.valid && p.isStore)
    pushStore(p.robTag, p.nBytes, CPUstate);
  // store value ready: write the value from the PRF (RS owns the slot,
  // it frees it in its own tick)
  for (int i = 0; i < STORERS_CAP; ++i) {
    if (!input.RSModule.storeValueRS[i].free &&
        input.PRFModule.isOperandReady(input.RSModule.storeValueRS[i].data)) {
      uint8_t SeqTag = input.RSModule.storeValueRS[i].robTag;
      if (!input.squashDetect.needSquash ||
          (input.squashDetect.needSquash &&
           ROB::isOlder(SeqTag, input.squashDetect.SquashTag))) {
        writeValue(
            input.PRFModule.getOperandValue(
                input.RSModule.storeValueRS[i].data),
            memSlot(input.RSModule.storeValueRS[i].memIndex), CPUstate);
      }
    }
  }
  // store address ready: write the address from the AGU result
  if (!input.AGUModule.isEmpty() &&
      isStoreMem(input.AGUModule.headMemIndex())) {
    auto aguRobTag = input.AGUModule.headRobTag();
    if (!input.squashDetect.needSquash ||
        (input.squashDetect.needSquash &&
         ROB::isOlder(aguRobTag, input.squashDetect.SquashTag))) {
      writeAddress(
          input.AGUModule.headValue(),
          memSlot(input.AGUModule.headMemIndex()), CPUstate);
    }
  }
  // dispatch decision apply: store sent to DMEM
  const auto &decision = input.decision;
  bool storeDispatched = false;
  if (decision.valid && decision.request.op == Operation::Store)
    storeDispatched = true;
  if (storeDispatched)
    pop(CPUstate);
  if (input.ROBModule.storeWillCommit(input.squashDetect)) {
    const auto storeTag = input.ROBModule.getHead();
    for (int i = 0; i < SQ_CAP; ++i) {
      if (isActive(i) && SQqueue[i].robTag == storeTag &&
          !SQqueue[i].isCommitted) {
        setCommitted(i, CPUstate);
      }
    }
  }
  // flush on squash
  if (input.squashDetect.needSquash &&
      input.ROBModule.matchesTag(input.squashDetect.SquashTag)) {
    flush(input.ROBModule.getSqTailSnapshot(robSlot(input.squashDetect.SquashTag)), CPUstate);
  }
}
