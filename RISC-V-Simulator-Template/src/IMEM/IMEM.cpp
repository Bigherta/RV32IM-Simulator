#include "../include/IMEM.hpp"
#include "common.h"
#include <cassert>
#include <cstdint>
void IMEM::work() {
  if (needSquash) {
    // original clear() memset the whole table; stale mid-countdown slots
    // would keep decrementing and double-write remainCycle when a later push
    // wraps onto them, so every slot's valid must be dropped
    for (int i = 0; i < IMEM_CAP; ++i)
      IMEMreqs[i].valid <= 0;
    head <= 0;
  } else {
  // stage 3 pop: release the queue head once the returned line is consumed
  // by ICache (write-own-only). Clearing the head bit IS the occupancy
  // decrement -- there is no counter register.
  if (lineConsumed) {
    IMEMreqs[static_cast<uint32_t>(head)].valid <= 0;
    IMEMreqs[static_cast<uint32_t>(head)].remainCycle <= 0;
    head <= ((static_cast<uint32_t>(head) + 1) & (IMEM_CAP - 1));
  }
  // stage 1 claim the fetch-line request (write-own-only, mirrors DMEM
  // !busy && decision.valid). Slot index is invariant under the pop above:
  // both derive from the committed (_M_old) bitmap, and (head+1)+(occ-1)
  // == head+occ.
  if (fetchValid) {
    auto occ = occupancy();
    assert(occ < static_cast<uint32_t>(IMEM_CAP));
    auto idx = (static_cast<uint32_t>(head) + occ) & (IMEM_CAP - 1);
    IMEMreqs[idx].remainCycle <= MEM_LATENCY;
    IMEMreqs[idx].lineAddr <= fetchPC;
    IMEMreqs[idx].valid <= 1; // setting this bit IS the occupancy increment
  }
  // pipeline decrement: fixed-length scan with no break (RTL dataflow
  // semantics)
  for (int i = 0; i < IMEM_CAP; ++i) {
    if (static_cast<bool>(IMEMreqs[i].valid) &&
        static_cast<uint32_t>(IMEMreqs[i].remainCycle) != 0) {
      auto next = static_cast<uint32_t>(IMEMreqs[i].remainCycle) - 1;
      IMEMreqs[i].remainCycle <= next;
      if (next == 0) {
        // line burst fill: one wide read per word (4x32-bit)
        for (int w = 0; w < (CACHE_BLOCK_CAP >> 2); ++w) {
          IMEMreqs[i].Data[w] <= read_word(
              static_cast<uint32_t>(IMEMreqs[i].lineAddr) +
              static_cast<uint32_t>(w << 2));
        }
      }
    }
  }
  }
}