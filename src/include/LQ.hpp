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
struct IssuePacket;
struct LQEntry {
  RobTag robTag;
  uint32_t address;
  int32_t value;
  int n_bytes;
  bool isAddressReady;
  ValueState valueState;
  bool isUnsigned;
  bool isCDBBroadcast;
};
struct LQInput {
  SquashInfo squashDetect;
  const AGU &AGUModule;
  const ROB &ROBModule;
  const IssuePacket &issuePacket;
  const SQ &SQModule;
  lqCDB cdbOutput;
  LoadResponse loadResp;
  MemDispatchDecision decision;
  StoreNotify storeNotifies[STORERS_CAP];
  StoreNotify storeAddrNotify;
  LQInput(const AGU &agu, const ROB &rob, const SQ &sq,
          const IssuePacket &pkt)
      : AGUModule(agu), ROBModule(rob), issuePacket(pkt), SQModule(sq) {}
};
class LQ {
private:
  LQEntry LQqueue[LQ_CAP];
  uint8_t head = 0;
  uint8_t tail = 0;
  void pushLoad(RobTag robTag, int n_bytes, bool isUnsigned);
  void pop();
  void writeAddress(uint32_t address, int index);
  void writeValue(int32_t value, int index);
  void writeValueIfFetching(uint8_t robTag, int index, int32_t value);
  void setValueState(int index, ValueState state);
  void setCDBBroadcast(int index);
  void applyStoreForward(const StoreNotify &notify);
  void flush(uint8_t tailSnapshot);

public:
  LQ() { std::memset(this, 0, sizeof(*this)); }
  bool isEmpty() const;
  bool isFull() const;
  bool isActive(uint8_t index) const;
  uint8_t getHead() const;
  uint8_t getTail() const;
  // Occupancy boundary AFTER this cycle's own enqueue (the caller pushes
  // exactly one entry at [tail] later this cycle). Distinct from the raw
  // tail: memIndex wants "my slot" (= old tail), squash snapshots want
  // "the border that keeps me alive" (= old tail + 1).
  uint8_t getTailSnapshot() const { return (tail + 1) & LQ_MASK; }
  auto getAddress(int index) const -> uint32_t;
  auto getValue(int index) const -> int32_t;
  auto headRobTag() const -> uint8_t;
  auto getRobTag(int index) const -> uint8_t;
  auto getIsUnsigned(int index) const -> bool;
  auto getNBytes(int index) const -> int;
  bool isAddressReady(int index) const { return LQqueue[index].isAddressReady; }
  ValueState getValueState(int index) const {
    return LQqueue[index].valueState;
  }
  int CDBDetect() const;
  int LoadDetect() const;
  void tick(const LQInput &, systemState &);
};
