#pragma once
#include "SQ.hpp"
#include "common.h"
#include "module.h"
#include <array>
#include <cstdint>
struct systemState;
struct AGU;
struct RSUnit;
struct ROB;
struct DMEM;
struct LQEntry {
  Register<7> robTag;
  Register<32> address;
  Register<32> value;
  Register<2> n_bytes;
  Register<1> isAddressReady;
  Register<2> valueState;
  Register<1> isUnsigned;
  Register<1> isCDBBroadcast;
};
struct LQInputSquash {
  Wire<1> needSquash;
  Wire<7> SquashTag;
};
struct LQInputIssue {
  Wire<1> issueValid;
  Wire<1> issueLoad;
  Wire<7> issueTag;
  Wire<2> issueBytes;
  Wire<1> issueIsUnsigned;
};
struct LQInputCDB {
  Wire<1> lsqSetCDB;
  Wire<7> cdbMemIndex;
};
struct LQInputAGU {
  Wire<1> isAGUEmpty;
  Wire<7> aguHeadMemIndex;
  Wire<7> aguHeadRobTag;
  Wire<32> aguHeadValue;
  Wire<1> sqReplyValid;
  Wire<32> sqReplyValue;
};
struct LQInputROB {
  Wire<1> isROBEmpty;
  Wire<7> robHeadTag;
  Wire<7> squashLQTailSnapshot;
};
struct LQInputLoadResp {
  Wire<1> loadRespValid;
  Wire<7> loadRespMemIndex;
  Wire<7> loadRespRobTag;
  Wire<32> loadRespValue;
};
struct LQInputMemDispatch {
  Wire<1> memDispatchValid;
  Wire<1> memDispatchIsLoad;
  Wire<7> memDispatchMemIndex;
};
struct LQInputStoreNotifies {
  // per-slot data-forward notify (8 slots) — sn prefix retained from prior design
  std::array<Wire<1>, STORERS_CAP> snValid;
  std::array<Wire<7>, STORERS_CAP> snStoreTag;
  std::array<Wire<32>, STORERS_CAP> snAddr;
  std::array<Wire<32>, STORERS_CAP> snValue;
  std::array<Wire<1>, STORERS_CAP> snFoundKnownSame;
  std::array<Wire<7>, STORERS_CAP> snKnownTag;
  std::array<Wire<1>, STORERS_CAP> snFoundUnknown;
  std::array<Wire<7>, STORERS_CAP> snUnknownTag;
  // single address-forward notify — san prefix retained
  Wire<1> sanValid;
  Wire<7> sanStoreTag;
  Wire<32> sanAddr;
  Wire<32> sanValue;
  Wire<1> sanFoundKnownSame;
  Wire<7> sanKnownTag;
  Wire<1> sanFoundUnknown;
  Wire<7> sanUnknownTag;
};
struct LQInput {
  LQInputSquash squash;
  LQInputIssue issue;
  LQInputCDB cdb;
  LQInputAGU agu;
  LQInputROB rob;
  LQInputLoadResp loadResp;
  LQInputMemDispatch memDispatch;
  LQInputStoreNotifies storeNotifies;
};
struct LQInner {
  std::array<LQEntry, LQ_CAP> LQqueue;
  Register<4> head;
  Register<4> tail;
};
class LQ : public dark::Module<LQInput, LQInner> {
  // (2026-09-01) The former private mutators (pushLoad/pop/writeAddress/
  // writeValue/writeValueIfFetching/setValueState/setCDBBroadcast/
  // applyStoreForward/flush) were inlined into work() as per-index write
  // intents: multi-source fields may only be assigned once per cycle, and
  // the intents reproduce the main tree's last-writer-wins order.

public:
  bool isEmpty() const;
  bool isFull() const;
  bool isActive(uint8_t index) const;
  uint8_t getHead() const;
  uint8_t getTail() const;
  // Occupancy boundary AFTER this cycle's own enqueue (the caller pushes
  // exactly one entry at [tail] later this cycle). Distinct from the raw
  // tail: memIndex wants "my slot" (= old tail), squash snapshots want
  // "the border that keeps me alive" (= old tail + 1).
  uint8_t getTailSnapshot() const { return static_cast<uint32_t>((tail + 1) & 0x0F); }
  auto getAddress(int index) const -> uint32_t;
  auto getValue(int index) const -> int32_t;
  auto headRobTag() const -> uint8_t;
  auto getRobTag(int index) const -> uint8_t;
  auto getIsUnsigned(int index) const -> bool;
  auto getNBytes(int index) const -> int;
  bool isAddressReady(int index) const { return static_cast<bool>(LQqueue[index].isAddressReady); }
  uint32_t getValueState(int index) const {
    return static_cast<uint32_t>(LQqueue[index].valueState);
  }
  int CDBDetect() const;
  int LoadDetect() const;
  void work() override;
};
