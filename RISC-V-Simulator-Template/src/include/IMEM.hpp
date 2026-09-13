#pragma once
#include "Memory.hpp"
#include "common.h"
#include "tools.h"
#include <array>
#include <cstdint>
#include <cstring>

static_assert(IMEM_CAP == 16); // head is a 4-bit ring pointer

struct IMEMInput {
  Wire<1> needSquash;
  Wire<1> fetchValid;
  Wire<32> fetchPC;
  Wire<1> lineConsumed;
};
struct IMEMOutput {
  Register<4> head;
};
struct IMEMInner {
  struct IMEMRequest {
    // line stored as 4x32-bit words (RTL: one 128-bit SRAM read port),
    // not as 16 individual bytes
    std::array<Register<32>, (CACHE_BLOCK_CAP >> 2)> Data;
    Register<32> lineAddr;
    Register<6> remainCycle;
    Register<1> valid;
  };
  std::array<IMEMRequest, IMEM_CAP> IMEMreqs;
};
struct IMEM : public Memory, dark::Module<IMEMInput, IMEMOutput, IMEMInner> {
  IMEM(const Memory &mem) : Memory(mem) {}
  // wide instruction-fetch read port: callers pass word-aligned addresses
  // (lineAddr is masked to 16B in CPU::wire(); RTL: addr[1:0] unwired).
  // Byte packing lives here and nowhere else on the fetch path; DMEM keeps
  // the byte-granularity interface (lb/sb need byte enables).
  uint32_t read_word(uint32_t addr) const {
    uint32_t w = 0;
    for (int b = 0; b < 4; ++b)
      w |= static_cast<uint32_t>(read_data(addr + static_cast<uint32_t>(b)))
           << (b << 3);
    return w;
  }
  // Window invariant: valid[i]==1 iff slot i lies in [head, head+occupancy)
  // (mod 16). Push/pop/squash maintain the bitmap itself, so the occupancy
  // count is a derived combinational view -- no counter register is stored.
  uint32_t occupancy() const {
    uint32_t n = 0;
    for (int i = 0; i < IMEM_CAP; ++i)
      n += static_cast<bool>(IMEMreqs[i].valid) ? 1u : 0u;
    return n;
  }
  bool isRequestFull() const { return occupancy() == IMEM_CAP; }
  bool isReturnReady() const {
    // occupancy>0 is implied by the head slot being valid (window invariant)
    return static_cast<bool>(
               IMEMreqs[static_cast<uint32_t>(head)].valid) &&
           static_cast<uint32_t>(
               IMEMreqs[static_cast<uint32_t>(head)].remainCycle) == 0;
  }
  // combinational line-return view over committed state; consumed through
  // ICacheInput.lineReturn Wire references (adds no pipeline stage).
  bool retValid() const { return isReturnReady(); }
  uint32_t retLineAddr() const {
    return static_cast<uint32_t>(
        IMEMreqs[static_cast<uint32_t>(head)].lineAddr);
  }
  uint32_t retWord(int w) const {
    return static_cast<uint32_t>(
        IMEMreqs[static_cast<uint32_t>(head)].Data[w]);
  }
  void work() override;
};