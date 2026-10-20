#include "../include/LQ.hpp"
#include "../include/CPU.hpp"
#include "../include/ROB.hpp"
#include "../include/common.hpp"
#include <cstdint>
#include <stdexcept>

bool LQ::isEmpty() const { return tail == head; }

bool LQ::isFull() const { return tail == (head ^ LQ_CAP); }

bool LQ::isActive(uint8_t index) const {
  if (head == tail)
    return false;
  return index < LQ_CAP &&
         ((index - head) & LQ_MASK) < ((tail - head) & LQ_SEQ_MASK);
}

void LQ::pop(systemState &CPUstate) const {
  CPUstate.LQModule.head = (head + 1) & LQ_SEQ_MASK;
}

void LQ::pushLoad(RobTag robTag, int nBytes, bool isUnsigned,
                  systemState &CPUstate) const {
  const auto slot = tail & LQ_MASK;
  CPUstate.LQModule.LQqueue[slot] = {};
  CPUstate.LQModule.LQqueue[slot].robTag = robTag;
  CPUstate.LQModule.LQqueue[slot].nBytes = nBytes;
  CPUstate.LQModule.LQqueue[slot].isUnsigned = isUnsigned;
  CPUstate.LQModule.LQqueue[slot].isAddressReady = false;
  CPUstate.LQModule.LQqueue[slot].valueState = ValueState::NOTREADY;
  CPUstate.LQModule.tail = (tail + 1) & LQ_SEQ_MASK;
}

uint8_t LQ::getHead() const { return head & LQ_MASK; }
uint8_t LQ::getTail() const { return tail; }

void LQ::flush(uint8_t tailSnapshot, systemState &CPUstate) const {
  CPUstate.LQModule.tail = tailSnapshot;
}

void LQ::writeAddress(uint32_t address, int index,
                      systemState &CPUstate) const {
  CPUstate.LQModule.LQqueue[index].address = address;
  CPUstate.LQModule.LQqueue[index].isAddressReady = true;
}

void LQ::writeValue(int32_t value, int index, uint8_t nBytes, bool isSigned,
                    systemState &CPUstate) const {
  // Raw data from every producer; width/sign are applied only at readout.
  // The immutable live row, not a second transport copy, owns these fields.
  (void)nBytes;
  (void)isSigned;
  CPUstate.LQModule.LQqueue[index].value = value;
  CPUstate.LQModule.LQqueue[index].valueState = ValueState::READY;
  CPUstate.LQModule.LQqueue[index].isCDBBroadcast = false;
}

void LQ::writeValueIfFetching(uint8_t robTag, int32_t value, int index,
                              uint8_t nBytes, bool isSigned,
                              systemState &CPUstate) const {
  if (LQqueue[index].robTag != robTag)
    return;
  if (LQqueue[index].valueState != ValueState::FETCHING)
    return;
  writeValue(value, index, nBytes, isSigned, CPUstate);
}

void LQ::setValueState(int index, ValueState state,
                       systemState &CPUstate) const {
  CPUstate.LQModule.LQqueue[index].valueState = state;
}

void LQ::setCDBBroadcast(int index, systemState &CPUstate) const {
  CPUstate.LQModule.LQqueue[index].isCDBBroadcast = true;
}

auto LQ::getAddress(int index) const -> uint32_t {
  if (LQqueue[index].isAddressReady)
    return LQqueue[index].address;
  throw std::runtime_error("Address is not ready!");
}

auto LQ::getValue(int index) const -> int32_t {
  if (LQqueue[index].valueState == ValueState::READY)
    return static_cast<int32_t>(extendLoad(static_cast<uint32_t>(LQqueue[index].value),
                                          LQqueue[index].nBytes, LQqueue[index].isUnsigned));
  throw std::runtime_error("Value is not ready!");
}

auto LQ::headRobTag() const -> uint8_t {
  return LQqueue[head & LQ_MASK].robTag;
}

auto LQ::getRobTag(int index) const -> uint8_t { return LQqueue[index].robTag; }

auto LQ::getIsUnsigned(int index) const -> bool {
  return LQqueue[index].isUnsigned;
}

auto LQ::getNBytes(int index) const -> int { return LQqueue[index].nBytes; }

int LQ::LoadDetect() const {
  if (head == tail)
    return 0xFFFFFFFF;
  int UnloadIndex = 0;
  bool foundUnload = false;
  for (int k = 0; k < LQ_CAP; k++) {
    uint8_t cur = (head + k) & LQ_MASK;
    if (!isActive(cur) || foundUnload)
      continue;
    if (LQqueue[cur].isAddressReady &&
        LQqueue[cur].valueState == ValueState::NOTREADY) {
      foundUnload = true;
      UnloadIndex = cur;
    }
  }
  return foundUnload ? UnloadIndex : 0xFFFFFFFF;
}

int LQ::CDBDetect() const {
  if (head == tail)
    return 0xFFFFFFFF;
  bool found = false;
  int detectedIndex = 0xFFFFFFFF;
  for (int k = 0; k < LQ_CAP; ++k) {
    uint8_t cur = (head + k) & LQ_MASK;
    if (!isActive(cur) || found)
      continue;
    if (LQqueue[cur].isAddressReady &&
        LQqueue[cur].valueState == ValueState::READY &&
        !LQqueue[cur].isCDBBroadcast) {
      detectedIndex = cur;
      found = true;
    }
  }
  return detectedIndex;
}

uint32_t LQ::applyStoreForward(const StoreNotify &notify,
                               systemState &CPUstate) const {
  uint32_t forwarded = 0;
  for (int k = 0; k < LQ_CAP; ++k) {
    uint8_t i = (head + k) & LQ_MASK;
    if (!isActive(i))
      break;
    if (!LQqueue[i].isAddressReady)
      continue;
    if (LQqueue[i].address != notify.addr || LQqueue[i].nBytes != notify.nBytes)
      continue;
    if (!ROB::isOlder(notify.storeTag, LQqueue[i].robTag))
      continue;
    bool blocked =
        (notify.foundKnownSame &&
         ROB::isOlder(notify.knownSameAddressOldestTag, LQqueue[i].robTag)) ||
        (notify.foundUnknown &&
         ROB::isOlder(notify.unknownOldestTag, LQqueue[i].robTag));
    if (!blocked) {
      writeValue(notify.value, i, LQqueue[i].nBytes, !LQqueue[i].isUnsigned,
                 CPUstate);
      forwarded |= 1u << i;
    }
  }
  return forwarded;
}

void LQ::tick(const LQInput &input, systemState &CPUstate) {
  uint32_t forwarded = 0; // Comb flag only: forwarding wins over a late reply.
  const auto &p = input.issuePacket;
  // store-forward broadcasts from SQ (data-ready events pre-computed in comb)
  for (int i = 0; i < STORERS_CAP; ++i)
    if (input.storeNotifies[i].valid)
      forwarded |= applyStoreForward(input.storeNotifies[i], CPUstate);
  if (input.storeAddrNotify.valid)
    forwarded |= applyStoreForward(input.storeAddrNotify, CPUstate);
  // AGU: load address ready -> write address + query SQ for forwarding
  if (input.executeEnabled && !input.AGUModule.isEmpty() &&
      !isStoreMem(input.AGUModule.headMemIndex())) {
    auto aguRobTag = input.AGUModule.headRobTag();
    const auto index = memSlot(input.AGUModule.headMemIndex());
    if (matchesAddress(index, aguRobTag) &&
        (!input.squashDetect.needSquash ||
         (input.squashDetect.needSquash &&
          ROB::isOlder(aguRobTag, input.squashDetect.SquashTag)))) {
      const uint32_t value = input.AGUModule.headValue();
      writeAddress(value, index, CPUstate);
      auto reply = input.SQModule.replyToLoadRequest(
          value, LQqueue[index].nBytes, aguRobTag);
      if (reply.valid) {
        writeValue(reply.value, index, LQqueue[index].nBytes,
                   !LQqueue[index].isUnsigned, CPUstate);
        forwarded |= 1u << index;
      }
    }
  }
  // dispatch decision apply
  const auto &decision = input.decision;
  if (decision.valid && decision.request.op == Operation::Load) {
    const auto index = memSlot(decision.request.memIndex);
    setValueState(index, ValueState::FETCHING, CPUstate);
    forwarded &= ~(1u << index);
  }
  // retire pop
  auto retiredHead = head;
  for (unsigned lane = 0; lane < COMMIT_WIDTH; ++lane) {
    if (lane < input.commits.count && retiredHead != tail &&
        LQqueue[retiredHead & LQ_MASK].robTag == input.commits.tags[lane])
      retiredHead = (retiredHead + 1) & LQ_SEQ_MASK;
  }
  if (retiredHead != head) CPUstate.LQModule.head = retiredHead;
  // load response from DMEM
  if (input.loadResp.valid) {
    auto index = memSlot(input.loadResp.memIndex);
    if (LQqueue[index].valueState == ValueState::FETCHING &&
        !(forwarded & (1u << index)))
      writeValueIfFetching(input.loadResp.robTag, input.loadResp.value, index,
                           LQqueue[index].nBytes, !LQqueue[index].isUnsigned,
                           CPUstate);
  }
  // CDB consume (LQ bus)
  if (input.cdbOutput.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOutput.robTag, input.squashDetect.SquashTag)) {
      setCDBBroadcast(memSlot(input.cdbOutput.memIndex), CPUstate);
    }
  }
  // Allocation owns the last write to an old inactive tail slot; a late
  // response for its previous occupant cannot initialize the new load.
  if (p.valid && p.isLoad)
    pushLoad(p.robTag, p.nBytes, p.isUnsigned, CPUstate);
  // flush on squash
  if (input.squashDetect.needSquash &&
      input.ROBModule.matchesTag(input.squashDetect.SquashTag))
    flush(input.ROBModule.getLqTailSnapshot(
              robSlot(input.squashDetect.SquashTag)),
          CPUstate);
}
