#include "../include/IMEM.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cassert>
void IMEM::clear() {
  memset(IMEMreqs, 0, sizeof(IMEMreqs));
  head = count = 0;
}

void IMEM::pop() {
  IMEMreqs[head].valid = false;
  IMEMreqs[head].remain_cycle = 0;
  head = (head + 1) & (IMEM_CAP - 1);
  --count;
}

void IMEM::pushRequest(uint32_t lineAddr) {
  assert(count != IMEM_CAP);
  auto idx = (head + count) & (IMEM_CAP - 1);
  IMEMRequest request{};
  request.remain_cycle = MEM_LATENCY;
  request.lineAddr = lineAddr;
  request.valid = true;
  IMEMreqs[idx] = request;
  ++count;
}

void IMEM::snapshotFrom(const IMEM &other) {
  memcpy(IMEMreqs, other.IMEMreqs, sizeof(IMEMreqs));
  head = other.head;
  count = other.count;
}

LineReturn IMEM::getReturn() const {
  LineReturn out;
  if (isReturnReady()) {
    out.valid = true;
    out.lineAddr = IMEMreqs[head].lineAddr;
    for (int w = 0; w < ICACHE_WORDS_PER_LINE; ++w) {
      uint32_t word = 0;
      for (int b = 0; b < RV32_WORD_BYTES; ++b) {
        word |= static_cast<uint32_t>(
                    IMEMreqs[head].data[(w << RV32_WORD_BYTE_BITS) + b])
                << (b << 3);
      }
      out.data[w] = word;
    }
  }
  return out;
}

void IMEM::tick(const IMEMInput &input, systemState &CPUstate) {
  if (input.squashDetect.needSquash) {
    for (int i = 0; i < IMEM_CAP; ++i) CPUstate.IMEMModule.IMEMreqs[i] = {};
    CPUstate.IMEMModule.head = 0;
    CPUstate.IMEMModule.count = 0;
    return;
  }
  auto nextHead = head;
  auto nextCount = count;
  // stage 3 pop: release the queue head once the returned line is consumed
  // by ICache (write-own-only)
  if (input.lineConsumed) {
    CPUstate.IMEMModule.IMEMreqs[head].valid = false;
    CPUstate.IMEMModule.IMEMreqs[head].remain_cycle = 0;
    nextHead = (head + 1) & (IMEM_CAP - 1);
    --nextCount;
  }
  // stage 1 claim the fetch-line request (write-own-only, mirrors DMEM
  // !busy && decision.valid)
  if (input.fetchDecision.valid) {
    const auto slot = (head + count) & (IMEM_CAP - 1);
    IMEMRequest request{};
    request.lineAddr = input.fetchDecision.pc;
    request.remain_cycle = MEM_LATENCY;
    request.valid = true;
    CPUstate.IMEMModule.IMEMreqs[slot] = request;
    ++nextCount;
  }
  // pipeline decrement: fixed-length scan with no break (RTL dataflow semantics)
  for (int i = 0; i < IMEM_CAP; ++i) {
    const auto &sreq = IMEMreqs[i];
    if (sreq.valid && sreq.remain_cycle > 0) {
      int next = sreq.remain_cycle - 1;
      CPUstate.IMEMModule.IMEMreqs[i].remain_cycle = next;
      if (next == 0) {
        // line burst fill: read the whole instruction line from Memory
        for (int b = 0; b < ICACHE_BLOCK_CAP; ++b) {
          CPUstate.IMEMModule.IMEMreqs[i].data[b] =
              read_data(sreq.lineAddr + b); // Immutable instruction image in the snapshot.
        }
      }
    }
  }
  CPUstate.IMEMModule.head = nextHead;
  CPUstate.IMEMModule.count = nextCount;
}
