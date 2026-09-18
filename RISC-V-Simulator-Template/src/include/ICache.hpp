#pragma once
#include "Memory.hpp" // MEM_SIZE guard below
#include "common.h"
#include "module.h"
#include "tools.h"
#include <array>
#include <cstdint>
#include <cstring>

static_assert(REQUEST_CAP == 4); // head is a 2-bit ring pointer
static_assert(MEM_SIZE == (1u << 17)); // tag = addr[17:13] must fit Register<4>

struct CacheLine {
  Register<1> valid;
  Register<4> tag; // addr[17:13] -- 8KB cache in 128KB space leaves 4 tag bits
  std::array<Register<32>, (CACHE_BLOCK_CAP >> 2)> Data; // 4 words per line
};

struct ICacheInput {
  Wire<1> needSquash;
  Wire<1> fetchValid;
  Wire<32> fetchPC;
  Wire<32> fetchPredictPC;
  Wire<6> fetchCkptId;
  Wire<1> popConsume;
  LineReturn lineReturn;
};

struct ICacheOutput {
  struct ICacheRequest {
    // valid marks the slot as occupied (window bitmap -> occupancy source);
    // ready marks the instruction as deliverable (hit or backfilled).
    // This split keeps placeholders inside the window so the derived-count
    // scheme stays exact.
    Register<32> raw_inst;
    Register<32> PC;
    Register<32> predictPC;
    Register<6> ckptId;
    Register<1> valid;
    Register<1> ready;
  };
  std::array<ICacheRequest, REQUEST_CAP> requestBuffer;
};

struct ICacheInner {
  std::array<CacheLine, CACHE_CAP> blocks;
  Register<2> head;
};

struct ICache : dark::Module<ICacheInput, ICacheOutput, ICacheInner> {
  // host-only cache profile counters (mirrors the main tree's ICache
  // hitCount/missCount). Incremented at the stage-1 push, once per accepted
  // fetch request. They are statistics, never read by the datapath, so they
  // cannot perturb the modelled behaviour.
  uint64_t statHits = 0;
  uint64_t statMisses = 0;

  // Window invariant over requestBuffer.valid[]; occupancy is derived, not
  // stored (same scheme as IMEM).
  uint32_t occupancy() const {
    uint32_t n = 0;
    for (int i = 0; i < REQUEST_CAP; ++i)
      n += static_cast<bool>(requestBuffer[i].valid) ? 1u : 0u;
    return n;
  }
  bool isRequestFull() const { return occupancy() == REQUEST_CAP; }
  // deliverable = occupied AND instruction present (old valid-bit semantics)
  bool isReturnReady() const {
    auto h = static_cast<uint32_t>(head);
    return static_cast<bool>(requestBuffer[h].valid) &&
           static_cast<bool>(requestBuffer[h].ready);
  }
  bool hit(uint32_t addr) const;
  // one 32-bit word of a cached line (Data is stored as words)
  uint32_t lineWord(uint32_t lineIdx, uint32_t w) const {
    return static_cast<uint32_t>(blocks[lineIdx].Data[w]);
  }
  auto returnRaw() const {
    return static_cast<uint32_t>(
        requestBuffer[static_cast<uint32_t>(head)].raw_inst);
  }
  auto returnPC() const {
    return static_cast<uint32_t>(requestBuffer[static_cast<uint32_t>(head)].PC);
  }
  auto returnPredictPC() const {
    return static_cast<uint32_t>(
        requestBuffer[static_cast<uint32_t>(head)].predictPC);
  }
  auto returnCkptId() const {
    return static_cast<uint32_t>(
        requestBuffer[static_cast<uint32_t>(head)].ckptId);
  }
  // combinational predicate: the ICache head holds the halt instruction.
  // Member function (not a stored Register) so it is visible the same cycle.
  bool isHaltSignal() const {
    return isReturnReady() && returnRaw() == 0x0ff00513;
  }
  void work() override;
};
