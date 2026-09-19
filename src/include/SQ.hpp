#pragma once
#include "common.hpp"
#include <cstdint>
#include <cstring>
struct systemState;
struct AGU;
struct RSUnit;
struct ROB;
struct DMEM;
struct LQ;
struct PRF;
struct IssuePacket;
struct SQEntry {
  RobTag robTag;
  uint32_t address;
  int32_t value;
  int n_bytes;
  bool isAddressReady;
  bool isValueReady;
};
struct SQInput {
  SquashInfo squashDetect;
  const AGU &AGUModule;
  const RSUnit &RSModule;
  const PRF &PRFModule;
  const ROB &ROBModule;
  const IssuePacket &issuePacket;
  MemDispatchDecision decision;
  SQInput(const AGU &agu, const RSUnit &rs, const PRF &prf, const ROB &rob,
          const IssuePacket &pkt)
      : AGUModule(agu), RSModule(rs), PRFModule(prf), ROBModule(rob),
        issuePacket(pkt) {}
};

struct StoreNotify {
  bool valid = false;
  uint8_t storeTag = 0;        // robTag of the broadcasting source store
  uint32_t addr = 0;           // store address (ready)
  int value = 0;               // store data
  bool foundKnownSame = false; // a younger store with the same known address exists
  uint8_t knownSameAddressOldestTag = 0; // robTag of the oldest of those
  bool foundUnknown = false;    // a younger store with unknown address exists
  uint8_t unknownOldestTag = 0; // robTag of the oldest of those
};

struct StoreResponse {
  bool valid = false;
  int value = 0;
};

class SQ {
private:
  SQEntry SQqueue[SQ_CAP];
  uint8_t head = 0;
  uint8_t tail = 0;
  void flush(uint8_t tailSnapshot);
  void pushStore(RobTag robTag, int n_bytes);
  void pop();
  void writeAddress(uint32_t address, int index);
  void writeValue(int32_t value, int index);

public:
  SQ() { std::memset(this, 0, sizeof(*this)); }
  bool isEmpty() const;
  bool isFull() const;
  bool isActive(uint8_t index) const;
  uint8_t getHead() const;
  uint8_t getTail() const;
  // Occupancy boundary AFTER this cycle's own enqueue (see LQ::getTailSnapshot).
  uint8_t getTailSnapshot() const { return (tail + 1) & SQ_MASK; }
  bool isReadyToCommit(int index) const;
  auto getAddress(int index) const -> uint32_t;
  auto getValue(int index) const -> int32_t;
  auto headRobTag() const -> uint8_t;
  auto getRobTag(int index) const -> uint8_t;
  auto getNBytes(int index) const -> int;
  bool isAddressReady(int index) const { return SQqueue[index].isAddressReady; }
  bool isValueReady(int index) const { return SQqueue[index].isValueReady; }
  auto planDataForward(int index, int32_t value) const -> StoreNotify;
  auto planAddressForward(int index, uint32_t address) const -> StoreNotify;
  auto replyToLoadRequest(uint32_t addr,
                          uint8_t loadTag) const -> StoreResponse;
  bool canDispatchLoad(uint32_t addr, RobTag loadTag) const;
  void tick(const SQInput &, systemState &);
};
