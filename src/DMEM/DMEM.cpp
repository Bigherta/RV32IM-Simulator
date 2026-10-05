#include "../include/DMEM.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cstdint>

void DMEM::snapshotFrom(const DMEM &other) {
  readBusy = other.readBusy;
  readBufferValid = other.readBufferValid;
  writeBusy = other.writeBusy;
  readExecute = other.readExecute;
  writeExecute = other.writeExecute;
  readOutputBuffer = other.readOutputBuffer;
}

int32_t DMEM::load_n_bytes(uint32_t address, int n, bool isSigned) const {
  uint32_t result = 0;
  for (int i = 0; i < 4; ++i) {
    if (i < n) {
      result |= static_cast<uint32_t>(read_data(address + i)) << (i << 3);
    }
  }
  if (isSigned && n == 1 && (result & 0x80u))
    result |= 0xFFFFFF00u;
  else if (isSigned && n == 2 && (result & 0x8000u))
    result |= 0xFFFF0000u;
  return static_cast<int32_t>(result);
}

void DMEM::writeLine(uint32_t addr, const uint8_t *lineData, systemState &CPUstate) const {
  for (int i = 0; i < DCACHE_BLOCK_CAP; i++) {
    const uint32_t address = addr + i;
    if (address < MEM_SIZE)
      CPUstate.DMEMModule.mem[address] = lineData[i];
  }
}

void DMEM::MemPull() { readBufferValid = false; }

bool DMEM::isReadBusy() const { return readBusy; }

bool DMEM::isWriteBusy() const { return writeBusy; }

bool DMEM::isReplyReady() const { return readBufferValid; }

std::array<uint8_t, DCACHE_BLOCK_CAP> DMEM::sampleReadCompletion() const {
  std::array<uint8_t, DCACHE_BLOCK_CAP> bytes{};
  if (readBusy && readExecute.remainCycle == 1) {
    const auto base = readExecute.address & ~DCACHE_OFFSET_MASK;
    for (int i = 0; i < DCACHE_BLOCK_CAP; ++i) bytes[i] = read_data(base + i);
  }
  return bytes;
}

void DMEM::tick(const DMEMInput &input, systemState &CPUstate) {
  // claim the pre-computed mem request (read = comb phase decided it)
    // read
    if (input.request.readValid  && !isReadBusy()) {
      CPUstate.DMEMModule.readExecute = input.request.read;
      CPUstate.DMEMModule.readBusy = true;
    }
    // write
    if (input.request.writeValid && !isWriteBusy()) {
      CPUstate.DMEMModule.writeExecute = input.request.write;
      CPUstate.DMEMModule.writeBusy = true;
    }
  // output stage: consume the previous cycle's reply
  if (isReplyReady()) {
    CPUstate.DMEMModule.readBufferValid = false;
  }
  // execution stage: this=snapshot reads own registers (hardware FSM),
  // writes the active module through the edge-write handle
  if (readBusy) {
    auto readExec = readExecute;
    readExec.remainCycle--;
    if (!readExec.remainCycle) {
      // Payload was sampled in comb(), before any edge writes.
      for (int i = 0; i < DCACHE_BLOCK_CAP; ++i)
        CPUstate.DMEMModule.readOutputBuffer.lineData[i] = input.completedReadLine[i];
      CPUstate.DMEMModule.readBufferValid = true;
      CPUstate.DMEMModule.readBusy = false;
    } else {
      CPUstate.DMEMModule.readExecute = readExec;
      CPUstate.DMEMModule.readBusy = true;
    }
  }
  if (writeBusy) {
    auto writeExec = writeExecute;
    writeExec.remainCycle--;
    if (!writeExec.remainCycle) {
      writeLine(writeExec.address, writeExec.lineData, CPUstate);
      CPUstate.DMEMModule.writeBusy = false;
    } else {
      CPUstate.DMEMModule.writeExecute = writeExec;
      CPUstate.DMEMModule.writeBusy = true;
    }
  }
}
