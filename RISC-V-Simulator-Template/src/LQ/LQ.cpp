#include "../include/LQ.hpp"
#include "../include/ROB.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

// Per-index write intent for the multi-source fields (value / valueState /
// isCDBBroadcast). The main tree's tick() runs its sources in a fixed order
// where the last writer of a field wins; a Register allows only a single write
// per cycle, so later sources overwrite the intent and the final single `<=`
// reproduces exactly that last-writer-wins semantics. (2026-09-01: CDB
// consume used to double-assign isCDBBroadcast when it coincided with a
// writeValue in the same cycle — e.g. a DCache loadResp arriving together
// with the CDB broadcast — which register.h:38 asserted on.)
struct LQWriteIntent {
  bool valWrite = false;
  uint32_t valData = 0;
  bool stateWrite = false;
  uint32_t stateData = 0;
  bool bcastWrite = false;
  bool bcastData = false;
};

} // namespace

bool LQ::isEmpty() const { return static_cast<uint32_t>(tail) == static_cast<uint32_t>(head); }

bool LQ::isFull() const { return ((static_cast<uint32_t>(tail) + 1) & 0x0F) == static_cast<uint32_t>(head); }

bool LQ::isActive(uint8_t index) const {
  if (static_cast<uint32_t>(head) == static_cast<uint32_t>(tail))
    return false;
  return ((index - static_cast<uint32_t>(head) + LQ_CAP) & 0x0F) < ((static_cast<uint32_t>(tail) - static_cast<uint32_t>(head) + LQ_CAP) & 0x0F);
}

uint8_t LQ::getHead() const { return static_cast<uint32_t>(head); }
uint8_t LQ::getTail() const { return static_cast<uint32_t>(tail); }

auto LQ::getAddress(int index) const -> uint32_t {
  if (LQqueue[index].isAddressReady)
    return static_cast<uint32_t>(LQqueue[index].address);
  throw std::runtime_error("Address is not ready!");
}

auto LQ::getValue(int index) const -> int32_t {
  if (static_cast<uint32_t>(LQqueue[index].valueState) ==
      static_cast<uint32_t>(ValueState::READY))
    return static_cast<uint32_t>(LQqueue[index].value);
  throw std::runtime_error("Value is not ready!");
}

auto LQ::headRobTag() const -> uint8_t {
  return static_cast<uint32_t>(LQqueue[static_cast<uint32_t>(head)].robTag);
}

auto LQ::getRobTag(int index) const -> uint8_t {
  return static_cast<uint32_t>(LQqueue[index].robTag);
}

auto LQ::getIsUnsigned(int index) const -> bool {
  return static_cast<bool>(LQqueue[index].isUnsigned);
}

auto LQ::getNBytes(int index) const -> int {
  uint32_t enc = static_cast<uint32_t>(LQqueue[index].n_bytes);
  return 1 << enc; // 0->1, 1->2, 2->4
}

int LQ::LoadDetect() const {
  if (static_cast<uint32_t>(head) == static_cast<uint32_t>(tail))
    return 0xFFFFFFFF;
  int UnloadIndex = 0;
  bool foundUnload = false;
  for (int k = 0; k < LQ_CAP; k++) {
    uint8_t cur = static_cast<uint32_t>((static_cast<uint32_t>(head) + k) & 0x0F);
    if (!isActive(cur) || foundUnload)
      continue;
    if (LQqueue[cur].isAddressReady &&
        static_cast<uint32_t>(LQqueue[cur].valueState) ==
            static_cast<uint32_t>(ValueState::NOTREADY)) {
      foundUnload = true;
      UnloadIndex = cur;
    }
  }
  return foundUnload ? UnloadIndex : 0xFFFFFFFF;
}

int LQ::CDBDetect() const {
  if (static_cast<uint32_t>(head) == static_cast<uint32_t>(tail))
    return 0xFFFFFFFF;
  bool found = false;
  int detectedIndex = 0xFFFFFFFF;
  for (int k = 0; k < LQ_CAP; ++k) {
    uint8_t cur = static_cast<uint32_t>((static_cast<uint32_t>(head) + k) & 0x0F);
    if (!isActive(cur) || found)
      continue;
    if (LQqueue[cur].isAddressReady &&
        static_cast<uint32_t>(LQqueue[cur].valueState) ==
            static_cast<uint32_t>(ValueState::READY) &&
        LQqueue[cur].isCDBBroadcast == 0) {
      detectedIndex = cur;
      found = true;
    }
  }
  return detectedIndex;
}

void LQ::work() {
  // --- combinational write-intent collection (rebuilt every cycle) ---
  std::array<LQWriteIntent, LQ_CAP> intent{};
  bool tailWritten = false;
  uint32_t tailData = 0;

  auto writeValueIntent = [&](int index, uint32_t value) {
    intent[index].valWrite = true;
    intent[index].valData = value;
    intent[index].stateWrite = true;
    intent[index].stateData = static_cast<uint32_t>(ValueState::READY);
    intent[index].bcastWrite = true;
    intent[index].bcastData = false;
  };
  // Effective state AFTER this cycle's earlier writes (main-tree tick() reads
  // the live entry mid-cycle; a Register only exposes the cycle-start value).
  auto stateAfter = [&](int index) -> uint32_t {
    if (intent[index].stateWrite)
      return intent[index].stateData;
    return static_cast<uint32_t>(LQqueue[index].valueState);
  };
  auto setStateIntent = [&](int index, uint32_t state) {
    intent[index].stateWrite = true;
    intent[index].stateData = state;
  };
  auto setBcastIntent = [&](int index) {
    intent[index].bcastWrite = true;
    intent[index].bcastData = true;
  };

  // 1. issue push — unique-field writes are direct (index = old tail, never
  //    reachable by later sources since it is inactive this cycle); the
  //    multi-writer fields and tail go through intents.
  if (static_cast<bool>(issue.issueValid) && static_cast<bool>(issue.issueLoad)) {
    auto t = static_cast<uint32_t>(tail);
    LQqueue[t].robTag <= static_cast<uint32_t>(issue.issueTag);
    LQqueue[t].n_bytes <= static_cast<uint32_t>(issue.issueBytes);
    LQqueue[t].isUnsigned <= static_cast<uint32_t>(issue.issueIsUnsigned);
    LQqueue[t].isAddressReady <= false;
    setStateIntent(static_cast<int>(t), static_cast<uint32_t>(ValueState::NOTREADY));
    intent[static_cast<int>(t)].bcastWrite = true;
    intent[static_cast<int>(t)].bcastData = false;
    tailWritten = true;
    tailData = (t + 1) & 0xF;
  }

  // 2. store-forward broadcasts from SQ (data-ready events pre-computed in comb)
  for (int i = 0; i < STORERS_CAP; ++i) {
    if (!static_cast<bool>(storeNotifies.snValid[i]))
      continue;
    StoreNotify n{};
    n.valid = true;
    n.storeTag = static_cast<uint32_t>(storeNotifies.snStoreTag[i]);
    n.addr = static_cast<uint32_t>(storeNotifies.snAddr[i]);
    n.value = static_cast<uint32_t>(storeNotifies.snValue[i]);
    n.foundKnownSame = static_cast<bool>(storeNotifies.snFoundKnownSame[i]);
    n.knownSameAddressOldestTag = static_cast<uint32_t>(storeNotifies.snKnownTag[i]);
    n.foundUnknown = static_cast<bool>(storeNotifies.snFoundUnknown[i]);
    n.unknownOldestTag = static_cast<uint32_t>(storeNotifies.snUnknownTag[i]);
    // applyStoreForward (intent version): conditions read cycle-start values,
    // exactly as the main tree reads the live entry before any AGU writes.
    for (int k = 0; k < LQ_CAP; ++k) {
      uint8_t cur = static_cast<uint32_t>((static_cast<uint32_t>(head) + k) & 0x0F);
      if (!isActive(cur))
        break;
      if (LQqueue[cur].isAddressReady == 0)
        continue;
      if (static_cast<uint32_t>(LQqueue[cur].address) != n.addr)
        continue;
      if (!ROB::isOlder(n.storeTag, static_cast<uint32_t>(LQqueue[cur].robTag)))
        continue;
      bool blocked =
          (n.foundKnownSame &&
           ROB::isOlder(n.knownSameAddressOldestTag, static_cast<uint32_t>(LQqueue[cur].robTag))) ||
          (n.foundUnknown &&
           ROB::isOlder(n.unknownOldestTag, static_cast<uint32_t>(LQqueue[cur].robTag)));
      if (!blocked) {
        writeValueIntent(static_cast<int>(cur), n.value);
      }
    }
  }
  if (static_cast<bool>(storeNotifies.sanValid)) {
    StoreNotify n{};
    n.valid = true;
    n.storeTag = static_cast<uint32_t>(storeNotifies.sanStoreTag);
    n.addr = static_cast<uint32_t>(storeNotifies.sanAddr);
    n.value = static_cast<uint32_t>(storeNotifies.sanValue);
    n.foundKnownSame = static_cast<bool>(storeNotifies.sanFoundKnownSame);
    n.knownSameAddressOldestTag = static_cast<uint32_t>(storeNotifies.sanKnownTag);
    n.foundUnknown = static_cast<bool>(storeNotifies.sanFoundUnknown);
    n.unknownOldestTag = static_cast<uint32_t>(storeNotifies.sanUnknownTag);
    for (int k = 0; k < LQ_CAP; ++k) {
      uint8_t cur = static_cast<uint32_t>((static_cast<uint32_t>(head) + k) & 0x0F);
      if (!isActive(cur))
        break;
      if (LQqueue[cur].isAddressReady == 0)
        continue;
      if (static_cast<uint32_t>(LQqueue[cur].address) != n.addr)
        continue;
      if (!ROB::isOlder(n.storeTag, static_cast<uint32_t>(LQqueue[cur].robTag)))
        continue;
      bool blocked =
          (n.foundKnownSame &&
           ROB::isOlder(n.knownSameAddressOldestTag, static_cast<uint32_t>(LQqueue[cur].robTag))) ||
          (n.foundUnknown &&
           ROB::isOlder(n.unknownOldestTag, static_cast<uint32_t>(LQqueue[cur].robTag)));
      if (!blocked) {
        writeValueIntent(static_cast<int>(cur), n.value);
      }
    }
  }

  // 3. AGU: load address ready -> write address + query SQ for forwarding
  if (!static_cast<bool>(agu.isAGUEmpty) &&
      !isStoreMem(static_cast<uint32_t>(agu.aguHeadMemIndex))) {
    auto aguRobTag = static_cast<uint32_t>(agu.aguHeadRobTag);
    if (!static_cast<bool>(squash.needSquash) ||
        ROB::isOlder(aguRobTag, static_cast<uint32_t>(squash.SquashTag))) {
      auto index = memSlot(static_cast<uint32_t>(agu.aguHeadMemIndex));
      LQqueue[index].address <= static_cast<uint32_t>(agu.aguHeadValue);
      LQqueue[index].isAddressReady <= true;
      if (static_cast<bool>(agu.sqReplyValid)) {
        writeValueIntent(static_cast<int>(index), static_cast<uint32_t>(agu.sqReplyValue));
      }
    }
  }

  // 4. dispatch decision apply
  if (static_cast<bool>(memDispatch.memDispatchValid) && static_cast<bool>(memDispatch.memDispatchIsLoad)) {
    setStateIntent(memSlot(static_cast<uint32_t>(memDispatch.memDispatchMemIndex)),
                   static_cast<uint32_t>(ValueState::FETCHING));
  }

  // 5. retire pop
  uint8_t cur = getHead();
  bool retireLoad = !static_cast<bool>(squash.needSquash) && cur != getTail() &&
                    (static_cast<bool>(rob.isROBEmpty) ||
                     ROB::isOlder(headRobTag(), static_cast<uint32_t>(rob.robHeadTag)));
  if (retireLoad) {
    head <= ((static_cast<uint32_t>(head) + 1) & 0xF);
  }

  // 6. load response — checks the state accumulated by this cycle's earlier
  //    writes (main-tree order), not the cycle-start snapshot. robTag is only
  //    written by pushLoad at the (inactive) old tail, so the old value is
  //    always equal to the live value here.
  if (static_cast<bool>(loadResp.loadRespValid)) {
    auto idx = memSlot(static_cast<uint32_t>(loadResp.loadRespMemIndex));
    if (static_cast<uint32_t>(LQqueue[idx].robTag) == static_cast<uint32_t>(loadResp.loadRespRobTag) &&
        stateAfter(static_cast<int>(idx)) == static_cast<uint32_t>(ValueState::FETCHING)) {
      writeValueIntent(static_cast<int>(idx), static_cast<uint32_t>(loadResp.loadRespValue));
    }
  }

  // 7. CDB consume — last writer of isCDBBroadcast wins (true overrides any
  //    writeValue's reset-to-false this cycle, matching the main-tree final
  //    state where setCDBBroadcast executes after writeValue).
  if (static_cast<bool>(cdb.lsqSetCDB)) {
    setBcastIntent(memSlot(static_cast<uint32_t>(cdb.cdbMemIndex)));
  }

  // 8. flush on squash — overrides the push's tail intent (main-tree order:
  //    flush runs last and wins).
  if (static_cast<bool>(squash.needSquash)) {
    tailWritten = true;
    tailData = static_cast<uint32_t>(rob.squashLQTailSnapshot);
  }

  // apply intents once
  for (int i = 0; i < LQ_CAP; ++i) {
    if (intent[i].valWrite)
      LQqueue[i].value <= intent[i].valData;
    if (intent[i].stateWrite)
      LQqueue[i].valueState <= intent[i].stateData;
    if (intent[i].bcastWrite)
      LQqueue[i].isCDBBroadcast <= intent[i].bcastData;
  }
  if (tailWritten)
    tail <= tailData;
}
