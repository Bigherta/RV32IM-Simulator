#pragma once
#include "../include/Memory.hpp"
#include "../include/common.hpp"
#include <cstdint>

struct systemState;

struct DMEMInput {
  DMEMRequest request;
  DMEMInput() = default;
};

// Data memory.
class DMEM : public Memory {
  bool readBusy = false;
  bool readBufferValid = false;
  bool writeBusy = false;

  ReadRequest readExecute = {};
  MemReply readOutputBuffer = {};
  WriteRequest writeExecute = {};
  void writeLine(uint32_t addr, const uint8_t* lineData);
  void MemPull();

public:
  DMEM() = default;
  DMEM(const Memory &mem) : Memory(mem) {}
  DMEM(const DMEM &) = default;
  DMEM &operator=(const DMEM &) = default;
  int32_t load_n_bytes(uint32_t address, int n, bool isSigned) const;
  bool isReadBusy() const;
  bool isWriteBusy() const;
  bool isReplyReady() const;
  // Read-only access to the completed request (readOutputBuffer). Used by the
  // DCache to relay the response; no execution-logic changes.
  const MemReply& reply() const { return readOutputBuffer; }
  void tick(const DMEMInput &, systemState &);
  void snapshotFrom(const DMEM &other);
};
