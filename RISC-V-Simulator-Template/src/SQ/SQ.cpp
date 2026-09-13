#include "../include/SQ.hpp"
#include "../include/ROB.hpp"
#include <cstdint>
#include <stdexcept>
#include <sys/types.h>

bool SQ::isEmpty() const {
  return static_cast<uint32_t>(tail) == static_cast<uint32_t>(head);
}

bool SQ::isFull() const {
  return ((static_cast<uint32_t>(tail) + 1) & 0x0F) ==
         static_cast<uint32_t>(head);
}

bool SQ::isActive(uint8_t index) const {
  if (static_cast<uint32_t>(head) == static_cast<uint32_t>(tail))
    return false;
  return ((static_cast<uint32_t>(index) - static_cast<uint32_t>(head) +
           SQ_CAP) &
          0x0F) <
         ((static_cast<uint32_t>(tail) - static_cast<uint32_t>(head) +
           SQ_CAP) &
          0x0F);
}

void SQ::pop() { head <= ((static_cast<uint32_t>(head) + 1) & 0xF); }

void SQ::pushStore(RobTag robTag, int n_bytes) {
  auto t = static_cast<uint32_t>(tail);
  SQqueue[t].robTag <= robTag;
  SQqueue[t].n_bytes <= static_cast<uint32_t>(n_bytes); // already 2b encoded
  SQqueue[t].isAddressReady <= false;
  SQqueue[t].isValueReady <= false;
  tail <= ((t + 1) & 0xF);
}

uint8_t SQ::getHead() const { return static_cast<uint32_t>(head); }
uint8_t SQ::getTail() const { return static_cast<uint32_t>(tail); }

void SQ::flush(uint8_t tailSnapshot) { tail <= tailSnapshot; }

void SQ::writeAddress(uint32_t address, int index) {
  SQqueue[index].address <= address;
  SQqueue[index].isAddressReady <= true;
}

void SQ::writeValue(int32_t value, int index) {
  SQqueue[index].value <= value;
  SQqueue[index].isValueReady <= true;
}

auto SQ::getAddress(int index) const -> uint32_t {
  if (SQqueue[index].isAddressReady)
    return static_cast<uint32_t>(SQqueue[index].address);
  throw std::runtime_error("Address is not ready!");
}

auto SQ::getValue(int index) const -> int32_t {
  if (SQqueue[index].isValueReady)
    return static_cast<int32_t>(static_cast<uint32_t>(SQqueue[index].value));
  throw std::runtime_error("Value is not ready!");
}

auto SQ::headRobTag() const -> uint8_t {
  return static_cast<uint32_t>(SQqueue[static_cast<uint32_t>(head)].robTag);
}

auto SQ::getRobTag(int index) const -> uint8_t {
  return static_cast<uint32_t>(SQqueue[index].robTag);
}

auto SQ::getNBytes(int index) const -> int {
  uint32_t enc = static_cast<uint32_t>(SQqueue[index].n_bytes);
  return 1 << enc; // 0->1B, 1->2B, 2->4B
}

auto SQ::planDataForward(int index, int32_t value) const -> StoreNotify {
  StoreNotify notify{};
  if (SQqueue[index].isAddressReady == 0)
    return notify;
  notify.storeTag = static_cast<uint32_t>(SQqueue[index].robTag);
  notify.addr = static_cast<uint32_t>(SQqueue[index].address);
  notify.value = value;
  uint8_t knownSameAddressOldestTag =
      static_cast<uint32_t>(SQqueue[index].robTag);
  uint8_t unknownOldestTag = static_cast<uint32_t>(SQqueue[index].robTag);
  bool FoundKnownSameAddressOldest = false;
  bool FoundUnknownOldest = false;
  for (int k = 1; k <= SQ_CAP; ++k) {
    uint8_t i = (index + k) & 0x0F;
    if (i == index || !isActive(i))
      continue;
    if (((i - index) & 0x0F) >= ((tail - index) & 0x0F))
      continue;
    if (SQqueue[i].address == SQqueue[index].address &&
        SQqueue[i].isAddressReady && !FoundKnownSameAddressOldest) {
      knownSameAddressOldestTag = static_cast<uint32_t>(SQqueue[i].robTag);
      FoundKnownSameAddressOldest = true;
    } else if (SQqueue[i].isAddressReady == 0 && !FoundUnknownOldest) {
      unknownOldestTag = static_cast<uint32_t>(SQqueue[i].robTag);
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
  if (SQqueue[index].isValueReady == 0)
    return notify;
  notify.storeTag = static_cast<uint32_t>(SQqueue[index].robTag);
  notify.addr = address;
  notify.value = static_cast<uint32_t>(SQqueue[index].value);
  uint8_t knownSameAddressOldestTag =
      static_cast<uint32_t>(SQqueue[index].robTag);
  uint8_t unknownOldestTag = static_cast<uint32_t>(SQqueue[index].robTag);
  bool FoundKnownSameAddressOldest = false;
  bool FoundUnknownOldest = false;
  for (int k = 1; k <= SQ_CAP; ++k) {
    uint8_t i = (index + k) & 0x0F;
    if (i == index || !isActive(i))
      continue;
    if (((i - index) & 0x0F) >= ((tail - index) & 0x0F))
      continue;
    if (SQqueue[i].address == address && SQqueue[i].isAddressReady &&
        !FoundKnownSameAddressOldest) {
      knownSameAddressOldestTag = static_cast<uint32_t>(SQqueue[i].robTag);
      FoundKnownSameAddressOldest = true;
    } else if (SQqueue[i].isAddressReady == 0 && !FoundUnknownOldest) {
      unknownOldestTag = static_cast<uint32_t>(SQqueue[i].robTag);
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
  uint8_t youngestSameAddrTag = 0;
  uint8_t youngestUnknownTag = 0;
  int forwardValue = 0;
  bool FoundSameAddr = false;
  bool FoundUnknown = false;
  bool SameAddrValueReady = false;
  for (int k = 0; k < SQ_CAP; k++) {
    auto index = static_cast<uint32_t>((head + k) & 0x0F);
    if (!isActive(index))
      break;
    if (ROB::isYounger(static_cast<uint32_t>(SQqueue[index].robTag), loadTag))
      break;
    if (SQqueue[index].isAddressReady == 0) {
      youngestUnknownTag = static_cast<uint32_t>(SQqueue[index].robTag);
      FoundUnknown = true;
    } else if (SQqueue[index].address == addr) {
      youngestSameAddrTag = static_cast<uint32_t>(SQqueue[index].robTag);
      FoundSameAddr = true;
      SameAddrValueReady = static_cast<bool>(SQqueue[index].isValueReady);
      if (SQqueue[index].isValueReady)
        forwardValue =
            static_cast<int32_t>(static_cast<uint32_t>(SQqueue[index].value));
    }
  }
  StoreResponse reply{};
  reply.valid = (SameAddrValueReady && FoundSameAddr && !FoundUnknown) ||
                (SameAddrValueReady && FoundSameAddr && FoundUnknown &&
                 ROB::isYounger(youngestSameAddrTag, youngestUnknownTag));
  reply.value = forwardValue;
  return reply;
}

bool SQ::canDispatchLoad(uint32_t addr, RobTag loadTag) const {
  bool hasSameAddressStore = false;
  for (int k = 0; k < SQ_CAP; k++) {
    if (hasSameAddressStore)
      continue;
    uint8_t cur = static_cast<uint32_t>((head + k) & 0xF);
    if (!isActive(cur))
      continue;
    if (!ROB::isOlder(static_cast<uint32_t>(SQqueue[cur].robTag), loadTag))
      continue;
    if (SQqueue[cur].isAddressReady && SQqueue[cur].address == addr)
      hasSameAddressStore = true;
  }
  return !hasSameAddressStore;
}

bool SQ::isReadyToCommit(int index) const {
  return SQqueue[index].isAddressReady && SQqueue[index].isValueReady;
}

void SQ::work() {
  // issue apply: push the pre-built store entry
  if (static_cast<bool>(issue.issueValid) && static_cast<bool>(issue.issueStore))
    pushStore(static_cast<uint32_t>(issue.issueTag),
              static_cast<uint32_t>(issue.issueBytes));
  // store value ready: write the value from the PRF (RS owns the slot, it
  // frees it in its own tick). svWriteValid already includes the squash guard.
  for (int i = 0; i < STORERS_CAP; ++i) {
    if (static_cast<bool>(prf.svWriteValid[i]))
      writeValue(static_cast<uint32_t>(prf.svValue[i]),
                 memSlot(static_cast<uint32_t>(prf.svMemIndex[i])));
  }
  // store address ready: write the address from the AGU result
  if (!static_cast<bool>(agu.isAGUEmpty) &&
      isStoreMem(static_cast<uint32_t>(agu.aguHeadMemIndex))) {
    auto aguRobTag = static_cast<uint32_t>(agu.aguHeadRobTag);
    if (!static_cast<bool>(squash.needSquash) ||
        ROB::isOlder(aguRobTag, static_cast<uint32_t>(squash.SquashTag))) {
      writeAddress(static_cast<uint32_t>(agu.aguHeadValue),
                   memSlot(static_cast<uint32_t>(agu.aguHeadMemIndex)));
    }
  }
  // dispatch decision apply: store sent to DMEM
  if (static_cast<bool>(memDispatch.memDispatchValid) &&
      static_cast<bool>(memDispatch.memDispatchIsStore)) {
    pop();
  }
  // flush on squash
  if (static_cast<bool>(squash.needSquash)) {
    flush(static_cast<uint32_t>(rob.squashSQTailSnapshot));
  }
}
