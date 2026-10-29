#pragma once
#include "SQ.hpp"
#include "common.hpp"
#include <cstdint>
#include <cstring>
struct systemState;
struct AGU;
struct RSUnit;
struct ROB;
struct DMEM;
struct IssueGroup;
// Combinational OLD AGU result for an already-reserved live LQ row.
// Not a new address stage and never an allocation-to-request bypass.
struct LoadAddressBypass {
  bool valid = false;
  uint8_t index = 0;
  RobTag robTag = 0;
  uint32_t address = 0;
  bool forwarded = false;
};
struct LoadResultCandidate {
  bool valid = false;
  uint8_t index = 0;
  RobTag robTag = 0;
  uint32_t value = 0;
};
struct LQEntry {
  RobTag robTag;
  uint32_t address;
  int32_t value;
  int nBytes;
  bool isAddressReady;
  ValueState valueState;
  bool isUnsigned;
  bool isCDBBroadcast;
};
struct LQInput {
  CommitPlan commits;
  bool executeEnabled = true;
  SquashInfo squashDetect;
  const AGU &AGUModule;
  const ROB &ROBModule;
  const IssueGroup &issuePacket;
  const SQ &SQModule;
  lqCDB cdbOutput;
  LoadResponse loadResp;
  MemDispatchDecision decision;
  StoreNotify storeNotifies[STORERS_CAP];
  StoreNotify storeAddrNotify;
  LQInput(const AGU &agu, const ROB &rob, const SQ &sq, const IssueGroup &pkt)
      : AGUModule(agu), ROBModule(rob), issuePacket(pkt), SQModule(sq) {}
};
class LQ {
private:
  LQEntry LQqueue[LQ_CAP];
  uint8_t head = 0;
  uint8_t tail = 0;
  void pushLoad(RobTag robTag, int nBytes, bool isUnsigned,
                systemState &CPUstate) const;
  void pop(systemState &CPUstate) const;
  void writeAddress(uint32_t address, int index, systemState &CPUstate) const;
  void writeValue(int32_t value, int index, uint8_t nBytes, bool isSigned,
                  systemState &CPUstate) const;
  void writeValueIfFetching(uint8_t robTag, int32_t value, int index,
                            uint8_t nBytes, bool isSigned,
                            systemState &CPUstate) const;
  void setValueState(int index, ValueState state, systemState &CPUstate) const;
  void setCDBBroadcast(int index, systemState &CPUstate) const;
  uint32_t applyStoreForward(const StoreNotify &notify,
                              systemState &CPUstate) const;
  bool notifyMatches(const StoreNotify &notify, int index) const;
  void flush(uint8_t tailSnapshot, systemState &CPUstate) const;

public:
  LQ() { std::memset(this, 0, sizeof(*this)); }
  bool isEmpty() const;
  bool isFull() const;
  unsigned count() const { return (tail - head) & LQ_SEQ_MASK; }
  bool isActive(uint8_t index) const;
  bool matchesAddress(uint8_t index, RobTag tag) const {
    return isActive(index) && getRobTag(index) == tag;
  }
  uint8_t getHead() const;
  uint8_t
  getTail() const; // Packed epoch+slot sequence; mask before memIndex use.
  // Occupancy boundary AFTER this cycle's own enqueue (the caller pushes
  // exactly one entry at [tail] later this cycle). Distinct from the raw
  // tail: memIndex wants "my slot" (= old tail), squash snapshots want
  // "the border that keeps me alive" (= old tail + 1).
  uint8_t getTailSnapshot() const { return (tail + 1) & LQ_SEQ_MASK; }
  auto getAddress(int index) const -> uint32_t;
  auto getValue(int index) const -> int32_t;
  auto headRobTag() const -> uint8_t;
  auto getRobTag(int index) const -> uint8_t;
  auto getIsUnsigned(int index) const -> bool;
  auto getNBytes(int index) const -> int;
  bool isAddressReady(int index) const { return LQqueue[index].isAddressReady; }
  int CDBDetect() const;
  LoadResultCandidate resultCandidate(const LQInput &input) const;
  LoadAddressBypass addressBypass(const AGU &agu, const SQ &sq,
                                 const SquashInfo &squash, bool enabled) const;
  uint32_t requestAddress(int index, const LoadAddressBypass &event = {}) const;
  int LoadDetect(const LoadAddressBypass &event = {}) const;
  void tick(const LQInput &, systemState &);
};
