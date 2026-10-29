#pragma once
#include "common.hpp"
#include <cstdint>
#include <cstring>
#include <type_traits>
struct systemState;
struct AGU;
struct RSUnit;
struct ROB;
struct DMEM;
struct LQ;
struct PRF;
struct IssueGroup;
static_assert(ROB_CAP <= 32, "Store completion bitmap fits its carrier");
using StoreCompletionMask = std::conditional_t<(ROB_CAP <= 16), uint16_t, uint32_t>;
struct StoreCompletion {
  StoreCompletionMask readyMask = 0;
  StoreCompletionMask epochMask = 0;
};
struct SQEntry {
  RobTag robTag;
  uint32_t address;
  int32_t value;
  int nBytes;
  bool isAddressReady;
  bool isValueReady;
  bool isCommitted = false;
};
struct SQInput {
  CommitPlan commits;
  bool retireEnabled = true;
  bool executeEnabled = true;
  SquashInfo squashDetect;
  const AGU &AGUModule;
  const RSUnit &RSModule;
  const PRF &PRFModule;
  const ROB &ROBModule;
  const IssueGroup &issuePacket;
  MemDispatchDecision decision;
  SQInput(const AGU &agu, const RSUnit &rs, const PRF &prf, const ROB &rob,
          const IssueGroup &pkt)
      : AGUModule(agu), RSModule(rs), PRFModule(prf), ROBModule(rob),
        issuePacket(pkt) {}
};

struct StoreNotify {
  bool valid = false;
  uint8_t storeTag = 0; // robTag of the broadcasting source store
  uint32_t addr = 0;    // store address (ready)
  int value = 0;        // store data
  uint8_t nBytes;       // store bytes
  bool foundKnownSame =
      false; // a younger store with the same known address exists
  uint8_t knownSameAddressOldestTag = 0; // robTag of the oldest of those
  bool foundUnknown = false;    // a younger store with unknown address exists
  uint8_t unknownOldestTag = 0; // robTag of the oldest of those
};

struct StoreResponse {
  bool valid = false;
  int value = 0;
  uint8_t startByte = 0;
};

class SQ {
private:
  SQEntry SQqueue[SQ_CAP];
  uint8_t head = 0;
  uint8_t tail = 0;
  void flush(uint8_t tailSnapshot, systemState &CPUstate) const;
  void pushStore(RobTag robTag, int nBytes, systemState &CPUstate) const;
  void pop(systemState &CPUstate) const;
  void writeAddress(uint32_t address, int index, systemState &CPUstate) const;
  void writeValue(int32_t value, int index, systemState &CPUstate) const;

public:
  SQ() { std::memset(this, 0, sizeof(*this)); }
  bool isEmpty() const;
  bool isFull() const;
  unsigned count() const { return (tail - head) & SQ_SEQ_MASK; }
  bool isActive(uint8_t index) const;
  bool matchesAddress(uint8_t index, RobTag tag) const {
    return isActive(index) && getRobTag(index) == tag;
  }
  uint8_t getHead() const;
  uint8_t getTail() const; // Packed epoch+slot sequence; mask before memIndex use.
  // Occupancy boundary AFTER this cycle's own enqueue (see
  // LQ::getTailSnapshot).
  uint8_t getTailSnapshot() const { return (tail + 1) & SQ_SEQ_MASK; }
  bool isReadyToCommit(int index) const;
  bool isCommitted(int index) const { return isActive(index) && SQqueue[index].isCommitted; }
  StoreCompletion completionBitmap() const;
  auto getAddress(int index) const -> uint32_t;
  auto getValue(int index) const -> int32_t;
  auto headRobTag() const -> uint8_t;
  auto getRobTag(int index) const -> uint8_t;
  auto getNBytes(int index) const -> int;
  bool isAddressReady(int index) const { return SQqueue[index].isAddressReady; }
  bool isValueReady(int index) const { return SQqueue[index].isValueReady; }
  auto planDataForward(int index, int32_t value) const -> StoreNotify;
  auto planAddressForward(int index, uint32_t address, RobTag producerTag) const -> StoreNotify;
  auto replyToLoadRequest(uint32_t addr, uint8_t nBytes,
                          uint8_t loadTag) const -> StoreResponse;
  // Called after forwarding fails; nBytes is the actual Load byte count.
  bool canSendLoadToCache(uint32_t addr, uint8_t nBytes, RobTag loadTag) const;
  void setCommitted(int index, systemState &CPUstate) const;
  void tick(const SQInput &, systemState &);
};
