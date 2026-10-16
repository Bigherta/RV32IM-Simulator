#pragma once
#include "../include/Memory.hpp"
#include "../include/common.hpp"
#include <cstdint>

struct systemState;

struct DMEMReadCompletion {
  std::array<uint8_t, DCACHE_BLOCK_CAP> line{};
  uint8_t slot = 0;
  bool valid = false;
};
struct DMEMInput {
  DMEMRequest request{};
  bool replyConsumed = false;
  bool writeConsumed = false;
  DMEMReadCompletion completedRead{};
};
struct DMEMEntry {
  // One line payload is reused for incoming write data or completed read data.
  MemReply payload{};
  int remainCycle = 0;
  bool isWrite = false;
  bool consumed = false;
};

// One mixed FIFO with DMEM_CAP total slots, shared by reads and writes. Up to
// one request of each kind can enter per clock; writes get the last free slot.
// Execution is parallel. Each kind's oldest reply can be consumed independently
// to avoid a read waiting for a write ACK behind it. Reclaim a consumed prefix.
class DMEM : public Memory {
  std::array<DMEMEntry, DMEM_CAP> requests{};
  uint8_t head = 0, count = 0;
  int oldestPending(bool write) const;
  void writeLine(const DMEMEntry &request, systemState &CPUstate) const;

public:
  DMEM() = default;
  DMEM(const Memory &mem) : Memory(mem) {}
  DMEM(const DMEM &) = default;
  DMEM &operator=(const DMEM &) = default;
  // Pass the concurrent write intent when arbitrating shared admission. Old
  // full still stalls even if replies will be consumed in this clock.
  bool canAcceptRead(bool writePending = false) const {
    return count + unsigned(writePending) < DMEM_CAP;
  }
  bool canAcceptWrite() const { return count < DMEM_CAP; }
  bool isReadBusy() const { return oldestPending(false) >= 0; }
  bool isWriteBusy() const { return oldestPending(true) >= 0; }
  bool isReplyReady() const;
  bool isWriteReplyReady() const;
  const MemReply &reply() const;
  MemWriteReply writeReply() const;
  void tick(const DMEMInput &, systemState &);
  void snapshotFrom(const DMEM &other);
  DMEMReadCompletion sampleReadCompletion() const;
};
