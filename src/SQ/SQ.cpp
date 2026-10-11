#include "../include/SQ.hpp"
#include "../include/CPU.hpp"
#include "../include/ROB.hpp"
#include <cstdint>
#include <cassert>
#include <stdexcept>

bool SQ::isEmpty() const { return tail == head; }

bool SQ::isFull() const { return tail == (head ^ SQ_CAP); }

bool SQ::isActive(uint8_t index) const {
  if (head == tail)
    return false;
  return index < SQ_CAP &&
         ((index - head) & SQ_MASK) < ((tail - head) & SQ_SEQ_MASK);
}

StoreCompletion SQ::completionBitmap() const {
  // Decode only old, live, address/data-ready, uncommitted Store identities.
  // Address/data delivered by this edge must wait until the next cycle.
  StoreCompletion completion{};
  for (int i = 0; i < SQ_CAP; ++i) {
    if (isActive(i) && isReadyToCommit(i) && !isCommitted(i)) {
      const auto tag = getRobTag(i);
      const uint32_t oneHot = uint32_t{1} << robSlot(tag);
      assert(!(completion.readyMask & oneHot));
      completion.readyMask |= oneHot;
      if (tag >> (ROB_TAG_WIDTH - 1)) completion.epochMask |= oneHot;
    }
  }
  return completion;
}

void SQ::pop(systemState &CPUstate) const {
  CPUstate.SQModule.head = (head + 1) & SQ_SEQ_MASK;
}

void SQ::pushStore(RobTag robTag, int nBytes, systemState &CPUstate) const {
  const auto slot = tail & SQ_MASK;
  CPUstate.SQModule.SQqueue[slot] = {};
  CPUstate.SQModule.SQqueue[slot].robTag = robTag;
  CPUstate.SQModule.SQqueue[slot].nBytes = nBytes;
  CPUstate.SQModule.SQqueue[slot].isAddressReady = false;
  CPUstate.SQModule.SQqueue[slot].isValueReady = false;
  CPUstate.SQModule.SQqueue[slot].isCommitted = false;
  CPUstate.SQModule.tail = (tail + 1) & SQ_SEQ_MASK;
}

uint8_t SQ::getHead() const { return head & SQ_MASK; }
uint8_t SQ::getTail() const { return tail; }

void SQ::flush(uint8_t tailSnapshot, systemState &CPUstate) const {
  CPUstate.SQModule.tail = tailSnapshot;
}

void SQ::writeAddress(uint32_t address, int index,
                      systemState &CPUstate) const {
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

auto SQ::headRobTag() const -> uint8_t {
  return SQqueue[head & SQ_MASK].robTag;
}

auto SQ::getRobTag(int index) const -> uint8_t { return SQqueue[index].robTag; }

auto SQ::getNBytes(int index) const -> int { return SQqueue[index].nBytes; }

auto SQ::planDataForward(int index, int32_t value) const -> StoreNotify {
  StoreNotify notify{};
  if (!SQqueue[index].isAddressReady)
    return notify;
  notify.storeTag = SQqueue[index].robTag;
  notify.addr = SQqueue[index].address;
  notify.value = value;
  notify.nBytes = SQqueue[index].nBytes;
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
    if ((SQqueue[i].address >> 2) == (SQqueue[index].address >> 2) &&
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

auto SQ::planAddressForward(int index, uint32_t address,
                            RobTag producerTag) const -> StoreNotify {
  StoreNotify notify{};
  if (!matchesAddress(index, producerTag) || !SQqueue[index].isValueReady)
    return notify;
  notify.storeTag = SQqueue[index].robTag;
  notify.addr = address;
  notify.value = SQqueue[index].value;
  notify.nBytes = SQqueue[index].nBytes;
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
    if ((SQqueue[i].address >> 2) == address >> 2 &&
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

auto SQ::replyToLoadRequest(uint32_t addr, uint8_t nBytes,
                            uint8_t loadTag) const -> StoreResponse {
  int youngestSameAddrOrder = -1;
  int youngestUnknownOrder = -1;
  int forwardValue = 0;
  bool SameAddrValueReady = false;
  bool SameAddrCover = false;
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
    } else {
      // Candidate = any byte-overlapping older/committed Store, not merely a
      // same-word one; the YOUNGEST candidate must cover the whole Load or
      // nothing forwards (single source, full cover -- no byte stitching).
      const uint32_t storeAddr = SQqueue[index].address;
      const uint8_t storeNBytes = SQqueue[index].nBytes;
      const bool overlaps = addr >= storeAddr
          ? addr - storeAddr < static_cast<uint32_t>(storeNBytes)
          : storeAddr - addr < nBytes;
      if (overlaps) {
        youngestSameAddrOrder = k;
        SameAddrValueReady = SQqueue[index].isValueReady;
        const uint32_t offset = addr - storeAddr;
        SameAddrCover = storeAddr <= addr && storeNBytes >= offset + nBytes;
        if (SQqueue[index].isValueReady && SameAddrCover)
          forwardValue = SQqueue[index].value >> (offset << 3);
      }
    }
  }
  StoreResponse reply{};
  reply.valid = SameAddrValueReady && SameAddrCover &&
                youngestSameAddrOrder > youngestUnknownOrder;
  reply.value = forwardValue;
  return reply;
}

bool SQ::canSendLoadToCache(uint32_t addr, uint8_t nBytes,
                            RobTag loadTag) const {
  bool hasOverlappingStore = false;
  bool hasUnknownAddressStore = false;
  for (int k = 0; k < SQ_CAP; k++) {
    if (hasOverlappingStore)
      continue;
    uint8_t cur = (head + k) & SQ_MASK;
    if (!isActive(cur))
      continue;
    if (SQqueue[cur].isCommitted)
      continue;
    if (!ROB::isOlder(SQqueue[cur].robTag, loadTag))
      continue;
    if (SQqueue[cur].isAddressReady) {
      const uint32_t storeAddr = SQqueue[cur].address;
      // Any byte overlap blocks an unforwarded Load, including partial
      // coverage in either direction. Differences avoid overflowing end addresses.
      hasOverlappingStore = addr >= storeAddr
          ? addr - storeAddr < static_cast<uint32_t>(SQqueue[cur].nBytes)
          : storeAddr - addr < nBytes;
    } else {
      hasUnknownAddressStore = true;
    }
  }
  return !hasOverlappingStore && !hasUnknownAddressStore;
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
        writeValue(input.PRFModule.getOperandValue(
                       input.RSModule.storeValueRS[i].data),
                   memSlot(input.RSModule.storeValueRS[i].memIndex), CPUstate);
      }
    }
  }
  // store address ready: write the address from the AGU result
  if (!input.AGUModule.isEmpty() &&
      isStoreMem(input.AGUModule.headMemIndex())) {
    auto aguRobTag = input.AGUModule.headRobTag();
    const auto index = memSlot(input.AGUModule.headMemIndex());
    if (matchesAddress(index, aguRobTag) &&
        (!input.squashDetect.needSquash ||
         (input.squashDetect.needSquash &&
          ROB::isOlder(aguRobTag, input.squashDetect.SquashTag)))) {
      writeAddress(input.AGUModule.headValue(), index, CPUstate);
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
    flush(input.ROBModule.getSqTailSnapshot(
              robSlot(input.squashDetect.SquashTag)),
          CPUstate);
  }
}
