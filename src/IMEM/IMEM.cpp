#include "../include/IMEM.hpp"
#include "../include/CPU.hpp"
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
  request.remain_cycle = 50; // main-memory latency (benchmarks.md)
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
    for (int w = 0; w < 4; ++w) {
      uint32_t word = 0;
      for (int b = 0; b < 4; ++b) {
        word |= static_cast<uint32_t>(IMEMreqs[head].data[(w << 2) + b])
                << (b << 3);
      }
      out.data[w] = word;
    }
  }
  return out;
}

void IMEM::tick(const IMEMInput &input, systemState &CPUstate) {
  if (input.squashDetect.needSquash) {
    CPUstate.IMEMModule.clear();
    return;
  }
  // stage 3 pop: release the queue head once the returned line is consumed
  // by ICache (write-own-only)
  if (input.lineConsumed) {
    CPUstate.IMEMModule.pop();
  }
  // stage 1 claim the fetch-line request (write-own-only, mirrors DMEM
  // !busy && decision.valid)
  if (input.fetchDecision.valid) {
    CPUstate.IMEMModule.pushRequest(static_cast<uint32_t>(input.fetchDecision.pc));
  }
  // pipeline decrement: fixed-length scan with no break (RTL dataflow semantics)
  for (int i = 0; i < IMEM_CAP; ++i) {
    const auto &sreq = IMEMreqs[i];
    if (sreq.valid && sreq.remain_cycle > 0) {
      int next = sreq.remain_cycle - 1;
      CPUstate.IMEMModule.IMEMreqs[i].remain_cycle = next;
      if (next == 0) {
        // line burst fill: read the whole 16B line from Memory
        for (int b = 0; b < ICACHE_BLOCK_CAP; ++b) {
          CPUstate.IMEMModule.IMEMreqs[i].data[b] =
              CPUstate.IMEMModule.read_data(sreq.lineAddr + b);
        }
      }
    }
  }
}