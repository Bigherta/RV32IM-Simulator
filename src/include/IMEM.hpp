#pragma once
#include "Memory.hpp"
#include "common.hpp"
#include <cstdint>
#include <cstring>

struct systemState;
struct IMEMInput {
  SquashInfo squashDetect;
  FetchDecision fetchDecision;
  bool lineConsumed = false;
};
struct IMEMRequest {
  uint8_t data[ICACHE_BLOCK_CAP];
  uint32_t lineAddr = 0;
  int remain_cycle = 0;
  bool valid = false;
};
class IMEM : public Memory {
private:
  IMEMRequest IMEMreqs[IMEM_CAP];
  uint8_t head = 0;
  uint8_t count = 0;
  void clear();
  void pushRequest(uint32_t lineAddr);
  void pop();

public:
  IMEM() { std::memset(IMEMreqs, 0, sizeof(IMEMreqs)); }
  IMEM(const Memory &mem) : Memory(mem) {
    std::memset(IMEMreqs, 0, sizeof(IMEMreqs));
  }
  IMEM(const IMEM &) = default;
  IMEM &operator=(const IMEM &) = default;
  bool isReturnReady() const {
    return count > 0 && IMEMreqs[head].valid &&
           IMEMreqs[head].remain_cycle == 0;
  }
  bool isRequestFull() const { return count == IMEM_CAP; }
  LineReturn getReturn() const;
  void tick(const IMEMInput &, systemState &);
  void snapshotFrom(const IMEM &other);
};
