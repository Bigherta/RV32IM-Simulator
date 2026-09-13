#pragma once
#include "common.h"
#include "module.h"
#include <array>
#include <cstdint>

struct SQEntry {
  Register<7> robTag;
  Register<32> address;
  Register<32> value;
  Register<2> n_bytes; // 0->1B, 1->2B, 2->4B (DMEM/LQ aligned)
  Register<1> isAddressReady;
  Register<1> isValueReady;
};

struct SQInputSquash {
  Wire<1> needSquash;
  Wire<7> SquashTag;
};
struct SQInputMemDispatch {
  Wire<1> memDispatchValid;
  Wire<1> memDispatchIsStore;
};
struct SQInputROB {
  Wire<7> squashSQTailSnapshot;
};
struct SQInputIssue {
  Wire<1> issueValid;
  Wire<1> issueStore;
  Wire<7> issueTag;
  Wire<2> issueBytes;
};
struct SQInputAGU {
  Wire<1> isAGUEmpty;
  Wire<7> aguHeadMemIndex;
  Wire<7> aguHeadRobTag;
  Wire<32> aguHeadValue;
};
struct SQInputPRF {
  std::array<Wire<1>, STORERS_CAP> svWriteValid; // value-ready + squash guard (wire-side)
  std::array<Wire<32>, STORERS_CAP> svValue;
  std::array<Wire<7>, STORERS_CAP> svMemIndex;
};
struct SQInput {
  SQInputSquash squash;
  SQInputIssue issue;
  SQInputMemDispatch memDispatch;
  SQInputROB rob;
  SQInputAGU agu;
  SQInputPRF prf;
};

// --- Output: combinational public views (assigns over committed state) ---
// StoreNotify packet: data-forward per storeValueRS slot (8) and the single
// AGU-head-driven address-forward.
struct SQOutputNotifyData {
  std::array<Wire<1>, STORERS_CAP> valid;
  std::array<Wire<7>, STORERS_CAP> storeTag;
  std::array<Wire<32>, STORERS_CAP> addr;
  std::array<Wire<32>, STORERS_CAP> value;
  std::array<Wire<1>, STORERS_CAP> foundKnownSame;
  std::array<Wire<7>, STORERS_CAP> knownTag;
  std::array<Wire<1>, STORERS_CAP> foundUnknown;
  std::array<Wire<7>, STORERS_CAP> unknownTag;
};
struct SQOutputNotifyAddr {
  Wire<1> valid;
  Wire<7> storeTag;
  Wire<32> addr;
  Wire<32> value;
  Wire<1> foundKnownSame;
  Wire<7> knownTag;
  Wire<1> foundUnknown;
  Wire<7> unknownTag;
};
// StoreResponse: the store-forward reply to the AGU-head load query.
struct SQOutputReply {
  Wire<1> valid;
  Wire<32> value;
};
struct SQOutput {
  SQOutputNotifyData data;
  SQOutputNotifyAddr addr;
  SQOutputReply reply;
};

struct SQInner {
  std::array<SQEntry, SQ_CAP> SQqueue;
  Register<4> head;
  Register<4> tail;
};

struct StoreNotify {
  bool valid = false;
  uint8_t storeTag = 0; // robTag of the broadcasting source store
  uint32_t addr = 0;    // store address (ready)
  int value = 0;        // store data
  bool foundKnownSame =
      false; // a younger store with the same known address exists
  uint8_t knownSameAddressOldestTag = 0; // robTag of the oldest of those
  bool foundUnknown = false;    // a younger store with unknown address exists
  uint8_t unknownOldestTag = 0; // robTag of the oldest of those
};

struct StoreResponse {
  bool valid = false;
  int value = 0;
};

struct SQ : public dark::Module<SQInput, SQOutput, SQInner> {
  void flush(uint8_t tailSnapshot);
  void pushStore(RobTag robTag, int n_bytes);
  void pop();
  void writeAddress(uint32_t address, int index);
  void writeValue(int32_t value, int index);

public:
  bool isEmpty() const;
  bool isFull() const;
  bool isActive(uint8_t index) const;
  uint8_t getHead() const;
  uint8_t getTail() const;
  // Occupancy boundary AFTER this cycle's own enqueue (see
  // LQ::getTailSnapshot).
  uint8_t getTailSnapshot() const {
    return static_cast<uint32_t>((tail + 1) & 0xF);
  }
  bool isReadyToCommit(int index) const;
  auto getAddress(int index) const -> uint32_t;
  auto getValue(int index) const -> int32_t;
  auto headRobTag() const -> uint8_t;
  auto getRobTag(int index) const -> uint8_t;
  auto getNBytes(int index) const -> int;
  bool isAddressReady(int index) const {
    return static_cast<bool>(SQqueue[index].isAddressReady);
  }
  bool isValueReady(int index) const {
    return static_cast<bool>(SQqueue[index].isValueReady);
  }
  auto planDataForward(int index, int32_t value) const -> StoreNotify;
  auto planAddressForward(int index, uint32_t address) const -> StoreNotify;
  auto replyToLoadRequest(uint32_t addr, uint8_t loadTag) const
      -> StoreResponse;
  bool canDispatchLoad(uint32_t addr, RobTag loadTag) const;
  void work() override;
};