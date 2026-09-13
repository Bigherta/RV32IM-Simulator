#include "../include/RS.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
int RSUnit::tryAllocInteger() const {
  for (int i = 0; i < INTEGERRS_CAP; i++)
    if (integerRS[i].free)
      return i;
  return -1;
}
int RSUnit::tryAllocLoad() const {
  for (int i = 0; i < LOADRS_CAP; i++)
    if (loadRS[i].free)
      return i;
  return -1;
}
int RSUnit::tryAllocStoreAddress() const {
  for (int i = 0; i < STORERS_CAP; i++)
    if (storeAddressRS[i].free)
      return i;
  return -1;
}
int RSUnit::tryAllocStoreValue() const {
  for (int i = 0; i < STORERS_CAP; i++)
    if (storeValueRS[i].free)
      return i;
  return -1;
}
int RSUnit::tryAllocBranch() const {
  for (int i = 0; i < BRANCHRS_CAP; i++)
    if (branchRS[i].free)
      return i;
  return -1;
}
int RSUnit::tryAllocMultiply() const {
  for (int i = 0; i < MULTIPLYRS_CAP; i++)
    if (multiplyRS[i].free)
      return i;
  return -1;
}
int RSUnit::tryAllocDivide() const {
  for (int i = 0; i < DIVIDERS_CAP; i++)
    if (divideRS[i].free)
      return i;
  return -1;
}
void RSUnit::tick(const RSInput &input, systemState &CPUstate) {
  const auto &p = input.issuePacket;
  if (p.valid) {
    if (p.hasInteger) {
      CPUstate.RSModule.integerRS[p.integerSlot] = p.integerRS;
    } else if (p.hasMultiply) {
      CPUstate.RSModule.multiplyRS[p.multiplySlot] = p.multiplyRS;
    } else if (p.hasDivide) {
      CPUstate.RSModule.divideRS[p.divideSlot] = p.divideRS;
    } else if (p.hasLoad) {
      CPUstate.RSModule.loadRS[p.loadSlot] = p.loadRS;
    } else if (p.hasStore) {
      CPUstate.RSModule.storeAddressRS[p.storeAddrSlot] = p.storeAddrRS;
      CPUstate.RSModule.storeValueRS[p.storeValueSlot] = p.storeValueRS;
    } else if (p.hasBranch) {
      CPUstate.RSModule.branchRS[p.branchSlot] = p.branchRS;
    }
  }
  if (input.dispatchBus.alu.valid) {
    int idx = input.dispatchBus.alu.rsIndex;
    CPUstate.RSModule.integerRS[idx].free = true;
    CPUstate.RSModule.integerRS[idx].src1 = {};
    CPUstate.RSModule.integerRS[idx].src2 = {};
  }
  if (input.dispatchBus.mul.valid) {
    int idx = input.dispatchBus.mul.rsIndex;
    CPUstate.RSModule.multiplyRS[idx].free = true;
    CPUstate.RSModule.multiplyRS[idx].src1 = {};
    CPUstate.RSModule.multiplyRS[idx].src2 = {};
  }
  if (input.dispatchBus.div.valid) {
    int idx = input.dispatchBus.div.rsIndex;
    CPUstate.RSModule.divideRS[idx].free = true;
    CPUstate.RSModule.divideRS[idx].src1 = {};
    CPUstate.RSModule.divideRS[idx].src2 = {};
  }
  if (input.dispatchBus.agu.valid) {
    int idx = input.dispatchBus.agu.rsIndex;
    if (input.dispatchBus.agu.rsType == RSType::Load) {
      CPUstate.RSModule.loadRS[idx].free = true;
      CPUstate.RSModule.loadRS[idx].src1 = {};
      CPUstate.RSModule.loadRS[idx].src2 = {};
    } else {
      CPUstate.RSModule.storeAddressRS[idx].free = true;
      CPUstate.RSModule.storeAddressRS[idx].src1 = {};
    }
  }
  if (input.dispatchBus.bru.valid) {
    int idx = input.dispatchBus.bru.rsIndex;
    CPUstate.RSModule.branchRS[idx].free = true;
    CPUstate.RSModule.branchRS[idx].src1 = {};
    CPUstate.RSModule.branchRS[idx].src2 = {};
  }
  // store value ready: the value now lives in the PRF (or is a constant);
  // once the operand is ready the RS slot can be released
  for (int i = 0; i < STORERS_CAP; i++) {
    if (!storeValueRS[i].free &&
        input.PRFModule.isOperandReady(storeValueRS[i].data)) {
      CPUstate.RSModule.storeValueRS[i].free = true;
    }
  }

  if (input.squashDetect.needSquash) {
    auto sqTag = input.squashDetect.SquashTag;
    for (int i = 0; i < INTEGERRS_CAP; i++) {
      if (!integerRS[i].free &&
          ROB::isOlder(sqTag, integerRS[i].robTag)) {
        CPUstate.RSModule.integerRS[i].free = true;
        CPUstate.RSModule.integerRS[i].src1 = {};
        CPUstate.RSModule.integerRS[i].src2 = {};
      }
    }
    for (int i = 0; i < MULTIPLYRS_CAP; i++) {
      if (!multiplyRS[i].free &&
          ROB::isOlder(sqTag, multiplyRS[i].robTag)) {
        CPUstate.RSModule.multiplyRS[i].free = true;
        CPUstate.RSModule.multiplyRS[i].src1 = {};
        CPUstate.RSModule.multiplyRS[i].src2 = {};
      }
    }
    for (int i = 0; i < DIVIDERS_CAP; i++) {
      if (!divideRS[i].free && ROB::isOlder(sqTag, divideRS[i].robTag)) {
        CPUstate.RSModule.divideRS[i].free = true;
        CPUstate.RSModule.divideRS[i].src1 = {};
        CPUstate.RSModule.divideRS[i].src2 = {};
      }
    }
    for (int i = 0; i < LOADRS_CAP; i++) {
      if (!loadRS[i].free &&
          ROB::isOlder(sqTag, loadRS[i].robTag)) {
        CPUstate.RSModule.loadRS[i].free = true;
        CPUstate.RSModule.loadRS[i].src1 = {};
        CPUstate.RSModule.loadRS[i].src2 = {};
      }
    }
    for (int i = 0; i < STORERS_CAP; i++) {
      if (!storeAddressRS[i].free &&
          ROB::isOlder(sqTag, storeAddressRS[i].robTag)) {
        CPUstate.RSModule.storeAddressRS[i].free = true;
        CPUstate.RSModule.storeAddressRS[i].src1 = {};
      }
    }
    for (int i = 0; i < BRANCHRS_CAP; i++) {
      if (!branchRS[i].free &&
          ROB::isOlder(sqTag, branchRS[i].robTag)) {
        CPUstate.RSModule.branchRS[i].free = true;
        CPUstate.RSModule.branchRS[i].src1 = {};
        CPUstate.RSModule.branchRS[i].src2 = {};
      }
    }
    for (int i = 0; i < STORERS_CAP; i++) {
      if (!storeValueRS[i].free &&
          ROB::isOlder(sqTag, storeValueRS[i].robTag)) {
        CPUstate.RSModule.storeValueRS[i].free = true;
      }
    }
  }
}
