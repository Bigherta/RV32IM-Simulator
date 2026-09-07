#include <iostream>
#include "../include/ROB.hpp"
#include <cstdint>

bool ROB::isOlder(RobTag tag_a, RobTag tag_b) {
  if ((tag_a >> 6) != (tag_b >> 6)) return ((tag_a & 63) > (tag_b & 63));
  return ((tag_a & 63) < (tag_b & 63));
}
bool ROB::isYounger(RobTag tag_a, RobTag tag_b) { return isOlder(tag_b, tag_a); }

ROB::ROB() { wire_output(); }

void ROB::wire_output() {
  headView.head = [this]() -> uint32_t { return static_cast<uint32_t>(robHead); };
  headView.nextTag = [this]() -> uint32_t { return static_cast<uint32_t>(next); };
  headView.isEmpty = [this]() -> uint32_t { return (static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next)) ? 1u : 0u; };
  headView.isFull = [this]() -> uint32_t { return ((static_cast<uint32_t>(robHead) ^ static_cast<uint32_t>(next)) == 0x40) ? 1u : 0u; };
  headView.isHeadCommitReady = [this]() -> uint32_t {
    if (static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next)) return 0u;
    return static_cast<bool>(ROBqueue[static_cast<uint32_t>(robHead) & 0x3F].isCommitReady) ? 1u : 0u;
  };
  headView.isHeadHalt = [this]() -> uint32_t {
    if (static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next)) return 0u;
    return static_cast<bool>(ROBqueue[static_cast<uint32_t>(robHead) & 0x3F].halt) ? 1u : 0u;
  };
  headView.headType = [this]() -> uint32_t {
    if (static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next)) return 0u;
    return static_cast<uint32_t>(ROBqueue[static_cast<uint32_t>(robHead) & 0x3F].type);
  };
  headView.headDest = [this]() -> uint32_t {
    if (static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next)) return 0u;
    return static_cast<uint32_t>(ROBqueue[static_cast<uint32_t>(robHead) & 0x3F].dest);
  };
  headView.haltCommitted = [this]() -> uint32_t { return static_cast<uint32_t>(robHaltCommitted); };
  headView.haltRd = [this]() -> uint32_t { return static_cast<uint32_t>(robHaltRd); };
  for (int i = 0; i < ROB_CAP; ++i) {
    entry.tag[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].tag); };
    entry.isCommitReady[i] = [this, i]() -> uint32_t { return static_cast<bool>(ROBqueue[i].isCommitReady) ? 1u : 0u; };
    entry.type[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].type); };
    entry.dest[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].dest); };
    entry.halt[i] = [this, i]() -> uint32_t { return static_cast<bool>(ROBqueue[i].halt) ? 1u : 0u; };
    entry.isCall[i] = [this, i]() -> uint32_t { return static_cast<bool>(ROBqueue[i].isCall) ? 1u : 0u; };
    entry.isRet[i] = [this, i]() -> uint32_t { return static_cast<bool>(ROBqueue[i].isRet) ? 1u : 0u; };
    entry.isIndirect[i] = [this, i]() -> uint32_t { return static_cast<bool>(ROBqueue[i].isIndirect) ? 1u : 0u; };
    entry.ckptId[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].ckptId); };
    entry.predictedPC[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].predictedPC); };
    entry.pc[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].pc); };
    entry.lqTailSnapshot[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].lqTailSnapshot); };
    entry.sqTailSnapshot[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].sqTailSnapshot); };
    entry.newPhy[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].newPhy); };
    entry.oldPhy[i] = [this, i]() -> uint32_t { return static_cast<uint32_t>(ROBqueue[i].oldPhy); };
  }
}

bool ROB::isHaltCommitted() const { return static_cast<bool>(robHaltCommitted); }
uint32_t ROB::getHaltRd() const { return static_cast<uint32_t>(robHaltRd); }
uint32_t ROB::getNextTag() const { return static_cast<uint32_t>(next); }
void ROB::updateNextTag() { next <= static_cast<uint32_t>((static_cast<uint32_t>(next) + 1) & 0x7F); }
bool ROB::isFull() const { return (static_cast<uint32_t>(robHead) ^ static_cast<uint32_t>(next)) == 0x40; }
bool ROB::isEmpty() const { return static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next); }
void ROB::pop() { robHead <= static_cast<uint32_t>((static_cast<uint32_t>(robHead) + 1) & 0x7F); }
uint32_t ROB::getTag(int index) const { return static_cast<uint32_t>(ROBqueue[index].tag); }
bool ROB::isCommitReadyAt(int index) const { return static_cast<bool>(ROBqueue[index].isCommitReady); }
ROBType ROB::getType(int index) const { return static_cast<ROBType>(static_cast<uint32_t>(ROBqueue[index].type)); }
uint32_t ROB::getDest(int index) const { return static_cast<uint32_t>(ROBqueue[index].dest); }
uint32_t ROB::getPC(int index) const { return static_cast<uint32_t>(ROBqueue[index].pc); }
bool ROB::isHalt(int index) const { return static_cast<bool>(ROBqueue[index].halt); }
uint32_t ROB::getCkptId(int index) const { return static_cast<uint32_t>(ROBqueue[index].ckptId); }
void ROB::setROBCommitReady(int index) {
  if (index < 0 || index >= ROB_CAP) return;
  ROBqueue[index].isCommitReady <= true;
}
uint32_t ROB::getPredictedPC(int index) const { return static_cast<uint32_t>(ROBqueue[index].predictedPC); }
uint32_t ROB::getLqTailSnapshot(int index) const { return static_cast<uint32_t>(ROBqueue[index].lqTailSnapshot); }
uint32_t ROB::getSqTailSnapshot(int index) const { return static_cast<uint32_t>(ROBqueue[index].sqTailSnapshot); }
uint32_t ROB::getNewPhy(int index) const { return static_cast<uint32_t>(ROBqueue[index].newPhy); }
uint32_t ROB::getOldPhy(int index) const { return static_cast<uint32_t>(ROBqueue[index].oldPhy); }
bool ROB::isCall(int index) const { return static_cast<bool>(ROBqueue[index].isCall); }
bool ROB::isRet(int index) const { return static_cast<bool>(ROBqueue[index].isRet); }
bool ROB::isIndirect(int index) const { return static_cast<bool>(ROBqueue[index].isIndirect); }
bool ROB::isHeadCommitReady() const {
  if (static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next)) return false;
  return static_cast<bool>(ROBqueue[static_cast<uint32_t>(robHead) & 0x3F].isCommitReady);
}
uint32_t ROB::getHead() const { return static_cast<uint32_t>(robHead); }
bool ROB::isHeadHalt() const {
  if (static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next)) return false;
  return isHalt(static_cast<uint32_t>(robHead) & 0x3F);
}
ROBType ROB::headType() const {
  if (static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next)) return ROBType::REGISTER;
  return getType(static_cast<uint32_t>(robHead) & 0x3F);
}
uint32_t ROB::headDest() const { return static_cast<uint32_t>(ROBqueue[static_cast<uint32_t>(robHead) & 0x3F].dest); }
void ROB::flush(uint32_t squashTag) { next <= static_cast<uint32_t>((squashTag + 1) & 0x7F); }

void ROB::work() {
  bool issueValid = static_cast<bool>(issue.issueValid);
  if (issueValid) {
    uint32_t q = static_cast<uint32_t>(next) & 0x3F;
    ROBqueue[q].tag <= static_cast<uint32_t>(next);
    ROBqueue[q].type <= static_cast<uint32_t>(issue.entry.type);
    ROBqueue[q].isCommitReady <= static_cast<uint32_t>(issue.entry.isCommitReady);
    ROBqueue[q].dest <= static_cast<uint32_t>(issue.entry.dest);
    ROBqueue[q].halt <= static_cast<uint32_t>(issue.entry.halt);
    ROBqueue[q].isCall <= static_cast<uint32_t>(issue.entry.isCall);
    ROBqueue[q].isRet <= static_cast<uint32_t>(issue.entry.isRet);
    ROBqueue[q].isIndirect <= static_cast<uint32_t>(issue.entry.isIndirect);
    ROBqueue[q].ckptId <= static_cast<uint32_t>(issue.entry.ckptId);
    ROBqueue[q].predictedPC <= static_cast<uint32_t>(issue.entry.predictedPC);
    ROBqueue[q].pc <= static_cast<uint32_t>(issue.entry.pc);
    ROBqueue[q].lqTailSnapshot <= static_cast<uint32_t>(issue.entry.lqTailSnapshot);
    ROBqueue[q].sqTailSnapshot <= static_cast<uint32_t>(issue.entry.sqTailSnapshot);
    ROBqueue[q].newPhy <= static_cast<uint32_t>(issue.entry.newPhy);
    ROBqueue[q].oldPhy <= static_cast<uint32_t>(issue.entry.oldPhy);
    updateNextTag();
  }

  bool needSquash = static_cast<bool>(squash.needSquash);
  uint32_t squashTag = static_cast<uint32_t>(squash.SquashTag);
  uint32_t curHead = static_cast<uint32_t>(robHead);
  bool curEmpty = (static_cast<uint32_t>(robHead) == static_cast<uint32_t>(next));
  int seen[20]; int nSeen = 0;
  auto markReady = [&](int slot) {
    if (slot < 0 || slot >= ROB_CAP) return;
    for (int k = 0; k < nSeen; ++k) if (seen[k] == slot) return;
    seen[nSeen++] = slot;
    ROBqueue[slot].isCommitReady <= true;
  };

  if (!static_cast<bool>(bru.isBRUEmpty)) {
    uint32_t brTag = static_cast<uint32_t>(bru.bruHeadRobTag);
    if (!needSquash || ROB::isOlder(brTag, squashTag)) {
      markReady(brTag & 0x3F);
    }
  }
  {
    uint32_t sqHead = static_cast<uint32_t>(sq.sqHead);
    for (int k = 0; k < MEMQ_SCAN_WINDOW; ++k) {
      uint32_t i = (sqHead + k) & 0x0F;
      if (!static_cast<bool>(sq.sqValid[i])) continue;
      if (!static_cast<bool>(sq.sqReadyToCommit[i])) continue;
      uint32_t sqTag = static_cast<uint32_t>(sq.sqRobTag[i]);
      if (needSquash && !ROB::isOlder(sqTag, squashTag)) continue;
      if (!curEmpty && ROB::isOlder(sqTag, curHead)) continue;
      if (curEmpty) continue;
      markReady(sqTag & 0x3F);
    }
  }
  if (static_cast<bool>(cdbOfALU.cdbValid)) {
    uint32_t cdbTag = static_cast<uint32_t>(cdbOfALU.cdbRobTag);
    if (!needSquash || ROB::isOlder(cdbTag, squashTag)) {
      if (!curEmpty && !ROB::isOlder(cdbTag, curHead)) {
        markReady(cdbTag & 0x3F);
      }
    }
  }
  if (static_cast<bool>(cdbOfLQ.cdbValid)) {
    uint32_t cdbTag = static_cast<uint32_t>(cdbOfLQ.cdbRobTag);
    if (!needSquash || ROB::isOlder(cdbTag, squashTag)) {
      if (!curEmpty && !ROB::isOlder(cdbTag, curHead)) {
        markReady(cdbTag & 0x3F);
      }
    }
  }
  if (static_cast<bool>(cdbOfMUL.cdbValid)) {
    uint32_t cdbTag = static_cast<uint32_t>(cdbOfMUL.cdbRobTag);
    if (!needSquash || ROB::isOlder(cdbTag, squashTag)) {
      if (!curEmpty && !ROB::isOlder(cdbTag, curHead)) {
        markReady(cdbTag & 0x3F);
      }
    }
  }

  if (needSquash) {
    flush(squashTag);
  } else {
    bool headReadyNow = false;
    if (!curEmpty) {
      if (static_cast<bool>(ROBqueue[curHead & 0x3F].isCommitReady)) headReadyNow = true;
    }
    if (!curEmpty && headReadyNow) {
      uint32_t oldIdx = curHead & 0x3F;
      bool oldHalt = static_cast<bool>(ROBqueue[oldIdx].halt);
      uint32_t oldDest = static_cast<uint32_t>(ROBqueue[oldIdx].dest);
      pop();
      if (oldHalt) {
        robHaltCommitted <= true;
        robHaltRd <= oldDest;
      }
    }
  }
}
