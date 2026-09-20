#include "../include/LQ.hpp"
#include "../include/CPU.hpp"
#include "../include/ROB.hpp"
#include "../include/common.hpp"
#include <cstdint>
#include <stdexcept>

bool LQ::isEmpty() const { return tail == head; }

bool LQ::isFull() const { return ((tail + 1) & LQ_MASK) == head; }

bool LQ::isActive(uint8_t index) const {
  if (head == tail)
    return false;
  return ((index - head + LQ_CAP) & LQ_MASK) <
         ((tail - head + LQ_CAP) & LQ_MASK);
}

void LQ::pop() { head = (head + 1) & LQ_MASK; }

void LQ::pushLoad(RobTag robTag, int n_bytes, bool isUnsigned) {
  LQqueue[tail] = {};
  LQqueue[tail].robTag = robTag;
  LQqueue[tail].n_bytes = n_bytes;
  LQqueue[tail].isUnsigned = isUnsigned;
  LQqueue[tail].isAddressReady = false;
  LQqueue[tail].valueState = ValueState::NOTREADY;
  tail = (tail + 1) & LQ_MASK;
}

uint8_t LQ::getHead() const { return head; }
uint8_t LQ::getTail() const { return tail; }

void LQ::flush(uint8_t tailSnapshot) { tail = tailSnapshot; }

void LQ::writeAddress(uint32_t address, int index) {
  LQqueue[index].address = address;
  LQqueue[index].isAddressReady = true;
}

void LQ::writeValue(int32_t value, int index) {
  LQqueue[index].value = value;
  LQqueue[index].valueState = ValueState::READY;
  LQqueue[index].isCDBBroadcast = false;
}

void LQ::writeValueIfFetching(uint8_t robTag, int index, int32_t value) {
  if (LQqueue[index].robTag != robTag)
    return;
  if (LQqueue[index].valueState != ValueState::FETCHING)
    return;
  writeValue(value, index);
}

void LQ::setValueState(int index, ValueState state) {
  LQqueue[index].valueState = state;
}

void LQ::setCDBBroadcast(int index) { LQqueue[index].isCDBBroadcast = true; }

auto LQ::getAddress(int index) const -> uint32_t {
  if (LQqueue[index].isAddressReady)
    return LQqueue[index].address;
  throw std::runtime_error("Address is not ready!");
}

auto LQ::getValue(int index) const -> int32_t {
  if (LQqueue[index].valueState == ValueState::READY)
    return LQqueue[index].value;
  throw std::runtime_error("Value is not ready!");
}

auto LQ::headRobTag() const -> uint8_t { return LQqueue[head].robTag; }

auto LQ::getRobTag(int index) const -> uint8_t { return LQqueue[index].robTag; }

auto LQ::getIsUnsigned(int index) const -> bool {
  return LQqueue[index].isUnsigned;
}

auto LQ::getNBytes(int index) const -> int { return LQqueue[index].n_bytes; }

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

void LQ::applyStoreForward(const StoreNotify &notify) {
  for (int k = 0; k < LQ_CAP; ++k) {
    uint8_t i = (head + k) & LQ_MASK;
    if (!isActive(i))
      break;
    if (!LQqueue[i].isAddressReady)
      continue;
    if (LQqueue[i].address != notify.addr)
      continue;
    if (!ROB::isOlder(notify.storeTag, LQqueue[i].robTag))
      continue;
    bool blocked =
        (notify.foundKnownSame &&
         ROB::isOlder(notify.knownSameAddressOldestTag, LQqueue[i].robTag)) ||
        (notify.foundUnknown &&
         ROB::isOlder(notify.unknownOldestTag, LQqueue[i].robTag));
    if (!blocked) {
      writeValue(notify.value, i);
    }
  }
}

void LQ::tick(const LQInput &input, systemState &CPUstate) {
  const auto &p = input.issuePacket;
  if (p.valid && p.isLoad)
    CPUstate.LQModule.pushLoad(p.robTag, p.nBytes, p.isUnsigned);
  // store-forward broadcasts from SQ (data-ready events pre-computed in comb)
  for (int i = 0; i < STORERS_CAP; ++i)
    if (input.storeNotifies[i].valid)
      CPUstate.LQModule.applyStoreForward(input.storeNotifies[i]);
  if (input.storeAddrNotify.valid)
    CPUstate.LQModule.applyStoreForward(input.storeAddrNotify);
  // AGU: load address ready -> write address + query SQ for forwarding
  if (!input.AGUModule.isEmpty() &&
      !isStoreMem(input.AGUModule.headMemIndex())) {
    auto aguRobTag = input.AGUModule.headRobTag();
    if (!input.squashDetect.needSquash ||
        (input.squashDetect.needSquash &&
         ROB::isOlder(aguRobTag, input.squashDetect.SquashTag))) {
      auto aguMemIndex = input.AGUModule.headMemIndex();
      const uint32_t value = input.AGUModule.headValue();
      auto index = memSlot(aguMemIndex);
      CPUstate.LQModule.writeAddress(value, index);
      auto reply = input.SQModule.replyToLoadRequest(value, aguRobTag);
      if (reply.valid) {
        CPUstate.LQModule.writeValue(reply.value, index);
      }
    }
  }
  // dispatch decision apply
  const auto &decision = input.decision;
  if (decision.valid && decision.request.op == Operation::Load)
    CPUstate.LQModule.setValueState(memSlot(decision.request.memIndex),
                                    ValueState::FETCHING);
  // retire pop
  uint8_t cur = getHead();
  bool retireLoad = !input.squashDetect.needSquash && cur != getTail() &&
                    (input.ROBModule.isEmpty() ||
                     ROB::isOlder(headRobTag(), input.ROBModule.getHead()));
  if (retireLoad)
    CPUstate.LQModule.pop();
  // load response from DMEM
  if (input.loadResp.valid)
    CPUstate.LQModule.writeValueIfFetching(input.loadResp.robTag,
                                           memSlot(input.loadResp.memIndex),
                                           input.loadResp.value);
  // CDB consume (LQ bus)
  if (input.cdbOutput.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOutput.robTag, input.squashDetect.SquashTag)) {
      CPUstate.LQModule.setCDBBroadcast(memSlot(input.cdbOutput.memIndex));
    }
  }
  // flush on squash
  if (input.squashDetect.needSquash &&
      input.ROBModule.matchesTag(input.squashDetect.SquashTag))
    CPUstate.LQModule.flush(
        input.ROBModule.getLqTailSnapshot(robSlot(input.squashDetect.SquashTag)));
}
