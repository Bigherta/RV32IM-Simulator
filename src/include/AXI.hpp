#pragma once
#include "Memory.hpp"
#include "common.hpp"
#include <array>
#include <vector>

struct systemState;
struct AXIMaster {
  bool arValid = false, rReady = false, awValid = false, wValid = false, bReady = false;
  uint32_t arAddr = 0, awAddr = 0, wData = 0;
  uint8_t wStrb = 0;
};
struct AXISlavePort {
  bool arReady = false, rValid = false, awReady = false, wReady = false, bValid = false;
  uint32_t rData = 0;
  uint8_t rResp = 0, bResp = 0;
};

struct AXIBusInput {
  AXIMaster fetch{}, data{};
  AXISlavePort slave{};
};
struct AXIBusPorts {
  AXIMaster slave{};
  AXISlavePort fetch{}, data{};
};
class AXIBus {
  std::array<bool, 64> owner{};
  uint8_t head = 0, count = 0;
  bool preferData = false;
  bool grantHeld = false, heldData = false;
public:
  AXIBusPorts ports(const AXIBusInput &) const;
  void tick(const AXIBusInput &, systemState &) const;
};

struct AXIFetchLine {
  std::array<uint32_t, ICACHE_WORDS_PER_LINE> words{};
  uint32_t address = 0;
  uint8_t issued = 0, collected = 0;
  bool valid = false;
};
struct AXIFetchInput {
  bool squash = false, fetchValid = false, lineConsumed = false;
  uint32_t fetchPC = 0;
  AXISlavePort bus{};
};
class AXIFetch {
  std::array<AXIFetchLine, IMEM_CAP> lines{};
  uint8_t head = 0;
  uint16_t inflight = 0, discard = 0;
  bool pending = false;
  uint32_t pendingAddr = 0;
  int unscheduled() const;
  int collecting() const;
public:
  unsigned occupancy() const;
  bool isRequestFull() const { return occupancy() == IMEM_CAP; }
  LineReturn getReturn() const;
  AXIMaster port() const;
  void tick(const AXIFetchInput &, systemState &) const;
};

struct AXIDataEntry {
  std::array<uint8_t, DCACHE_BLOCK_CAP> bytes{};
  uint32_t address = 0;
  uint8_t transaction = 0, issued = 0, collected = 0, aw = 0, w = 0;
  bool write = false, mmio = false;
};
struct AXIDataInput {
  DMEMRequest request{};
  bool replyConsumed = false, writeConsumed = false;
  bool exitValid = false;
  uint32_t exitValue = 0;
  AXISlavePort bus{};
};
// Independent read/write streams share 16 line credits. Responses retain MSHR
// identity; AW and W have independent cursors and never wait on each other.
class AXIData {
  std::array<AXIDataEntry, AXI_DEPTH> reads{}, writes{};
  uint8_t rh = 0, rn = 0, wh = 0, wn = 0;
  int readIssue() const;
  int writeIssue(bool address) const;
public:
  bool canAcceptRead(bool writePending = false) const {
    return rn + wn + unsigned(writePending) < AXI_DEPTH;
  }
  bool canAcceptWrite() const { return rn + wn < AXI_DEPTH; }
  bool isReplyReady() const;
  bool isWriteReplyReady() const;
  MemReply reply() const;
  MemWriteReply writeReply() const;
  bool exitReady() const { return rn == 0 && wn == 0; }
  AXIMaster port() const;
  void tick(const AXIDataInput &, systemState &) const;
};

struct AXIReadResponse { uint32_t data = 0, due = 0; uint8_t resp = 0; };
struct AXIWriteResponse { uint32_t result = 0, due = 0; uint8_t resp = 0; bool exit = false; };
struct AXIWriteData { uint32_t data = 0; uint8_t strb = 0; };
class Slave {
  std::vector<uint8_t> ram;
  std::array<uint32_t, AXI_DEPTH> ar{}, aw{};
  std::array<AXIWriteData, AXI_DEPTH> w{};
  std::array<AXIReadResponse, AXI_DEPTH> r{};
  std::array<AXIWriteResponse, AXI_DEPTH> b{};
  uint8_t ah = 0, an = 0, wh = 0, wn = 0, dh = 0, dn = 0, rh = 0, rn = 0, bh = 0, bn = 0;
  uint32_t cycle = 0, result = 0;
  uint32_t random = 1;
  bool done = false;
  std::array<uint8_t, 4> sampledRead{};
  static uint8_t response(uint32_t address);
public:
  explicit Slave(const Memory &);
  Slave() = default; // Snapshot contains channel state, not another RAM copy.
  void snapshotFrom(const Slave &);
  AXISlavePort port() const;
  bool isDone() const { return done; }
  uint32_t exitResult() const { return result; }
  void tick(const AXIMaster &, systemState &) const;
};
