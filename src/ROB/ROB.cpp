#include "../include/ROB.hpp"
#include "../include/CPU.hpp"
#include <cassert>
#include <cstdint>

bool ROB::isOlder(RobTag tag_a, RobTag tag_b) {
  if (tag_a >> (ROB_TAG_WIDTH - 1) != tag_b >> (ROB_TAG_WIDTH - 1)) {
    return (tag_a & ROB_INDEX_MASK) > (tag_b & ROB_INDEX_MASK);
  } else {
    return (tag_a & ROB_INDEX_MASK) < (tag_b & ROB_INDEX_MASK);
  }
}

bool ROB::isYounger(RobTag tag_a, RobTag tag_b) {
  return isOlder(tag_b, tag_a);
}

bool ROB::isHaltCommitted() const { return haltCommitted; }

int ROB::getHaltRd() const { return haltRd; }

uint8_t ROB::getNextTag() const { return next_tag; }

void ROB::updateNextTag(systemState &CPUstate) const {
  CPUstate.ROBModule.next_tag = robNextTag(next_tag);
}

bool ROB::isFull() const {
  // Packed tags repeat every ROB_CAP allocations, so "full" is exactly
  // "next points at the head slot one epoch ahead" (never {next == head},
  // which is empty).
  return robSlot(next_tag) == robSlot(head) && next_tag != head;
}

bool ROB::isEmpty() const { return head == next_tag; }

bool ROB::matchesTag(RobTag tag) const {
  const auto slot = robSlot(tag);
  const uint32_t span =
      (uint32_t(next_tag) - uint32_t(head)) & ROB_TAG_MASK;
  const uint32_t offset =
      (uint32_t(tag) - uint32_t(head)) & ROB_TAG_MASK;
  return slot < ROB_CAP && offset < span && ROBqueue[slot].tag == tag;
}

bool ROB::willCommit(const SquashInfo &squash) const {
  return !isEmpty() && isHeadCommitReady() &&
         (!squash.needSquash || isOlder(getHead(), squash.SquashTag));
}

bool ROB::storeWillCommit(const SquashInfo &squash) const {
  return willCommit(squash) && headType() == ROBType::STORE;
}

int ROB::push(ROBEntry entry, systemState &CPUstate) const {
  // The issue path must gate on isFull: a push into a full ROB silently
  // overwrites the head entry.
  assert(!isFull());
  entry.tag = next_tag;
  CPUstate.ROBModule.ROBqueue[robSlot(next_tag)] = entry;
  int index = robSlot(next_tag);
  updateNextTag(CPUstate);
  return index;
}

void ROB::pop(systemState &CPUstate) const {
  if (ROBqueue[robSlot(head)].exception) CPUstate.ROBModule.trapCommitted = true;
  CPUstate.ROBModule.head = robNextTag(head);
}

bool ROB::isCommitReadyAt(int index) const {
  return ROBqueue[index].isCommitReady;
}

ROBType ROB::getType(int index) const { return ROBqueue[index].type; }

int32_t ROB::getPC(int index) const { return ROBqueue[index].pc; }

bool ROB::isHalt(int index) const { return ROBqueue[index].halt; }

uint8_t ROB::getCkptId(int index) const { return ROBqueue[index].ckptId; }

int ROB::getPredictedPC(int index) const { return ROBqueue[index].predictedPC; }

uint8_t ROB::getLqTailSnapshot(int index) const {
  return ROBqueue[index].lqTailSnapshot;
}

uint8_t ROB::getSqTailSnapshot(int index) const {
  return ROBqueue[index].sqTailSnapshot;
}

uint8_t ROB::getNewPhy(int index) const { return ROBqueue[index].newPhy; }

uint8_t ROB::getOldPhy(int index) const { return ROBqueue[index].oldPhy; }

uint8_t ROB::getRd(int index) const { return ROBqueue[index].dest; }

bool ROB::isRet(int index) const { return ROBqueue[index].isRet; }

bool ROB::isCall(int index) const { return ROBqueue[index].isCall; }

bool ROB::isIndirect(int index) const { return ROBqueue[index].isIndirect; }

bool ROB::isHeadCommitReady() const {
  return ROBqueue[robSlot(getHead())].isCommitReady;
}

int ROB::getHead() const { return head; }

bool ROB::isHeadHalt() const { return isHalt(robSlot(getHead())); }

ROBType ROB::headType() const { return getType(robSlot(getHead())); }

int ROB::headDest() const { return ROBqueue[robSlot(head)].dest; }

void ROB::flush(RobTag squashTag, systemState &CPUstate) const {
  if (!matchesTag(squashTag))
    return;
  CPUstate.ROBModule.next_tag = robNextTag(squashTag);
}

void ROB::tick(const ROBInput &input, systemState &CPUstate) {
  const bool willCommitNow = willCommit(input.squashDetect);
  static_assert(ROB_CAP <= 32, "ROB completion bitmap fits its carrier");
  uint32_t readySetMask = 0;
  auto markReady = [&](RobTag tag) {
    const auto slot = robSlot(tag);
    if (slot >= ROB_CAP || (isEmpty() && !input.issuePacket.valid))
      return;
    const auto rowTag = input.issuePacket.valid && slot == robSlot(next_tag)
                            ? next_tag : ROBqueue[slot].tag;
    if (rowTag == tag) readySetMask |= uint32_t{1} << slot;
  };
  // BRU set ROB ready
  if (!input.BRUModule.isEmpty()) {
    uint8_t brRobTag = input.BRUModule.headRobTag();
    if (!input.squashDetect.needSquash ||
        (input.squashDetect.needSquash &&
         ROB::isOlder(brRobTag, input.squashDetect.SquashTag))) {
      markReady(brRobTag);
    }
  }
  // SQ supplies combinational slot/epoch masks from its old state. ROB owns
  // row identity/type/flush qualification and the single ready write.
  const auto &completion = input.storeCompletion;
  for (int slot = 0; slot < ROB_CAP; ++slot) {
    const uint32_t oneHot = uint32_t{1} << slot;
    const auto tag = ROBqueue[slot].tag;
    if ((completion.readyMask & oneHot) && matchesTag(tag) &&
        ROBqueue[slot].type == ROBType::STORE &&
        bool(completion.epochMask & oneHot) == bool(tag >> (ROB_TAG_WIDTH - 1)) &&
        (!input.squashDetect.needSquash || isOlder(tag, input.squashDetect.SquashTag)))
      readySetMask |= oneHot;
  }
  // CDB set ROB ready (dual)
  if (input.cdbOfALU.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfALU.robTag, input.squashDetect.SquashTag)) {
      markReady(input.cdbOfALU.robTag);
    }
  }
  if (input.cdbOfLQ.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfLQ.robTag, input.squashDetect.SquashTag)) {
      markReady(input.cdbOfLQ.robTag);
    }
  }
  if (input.cdbOfMul.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfMul.robTag, input.squashDetect.SquashTag)) {
      if (matchesTag(input.cdbOfMul.robTag)) {
        markReady(input.cdbOfMul.robTag);
      }
    }
  }
  if (input.cdbOfDiv.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfDiv.robTag, input.squashDetect.SquashTag)) {
      if (matchesTag(input.cdbOfDiv.robTag)) {
        markReady(input.cdbOfDiv.robTag);
      }
    }
  }
  // Single ready write per row, including initialization of a new allocation.
  if (input.issuePacket.valid) {
    ROBEntry entry = input.issuePacket.robEntry;
    entry.isCommitReady = entry.isCommitReady ||
                          bool(readySetMask & (uint32_t{1} << robSlot(next_tag)));
    push(entry, CPUstate);
  }
  for (int slot = 0; slot < ROB_CAP; ++slot) {
    if ((readySetMask & (uint32_t{1} << slot)) &&
        (!input.issuePacket.valid || slot != robSlot(next_tag)))
      CPUstate.ROBModule.ROBqueue[slot].isCommitReady = true;
  }
  // ROB squash
  if (input.squashDetect.needSquash) {
    flush(input.squashDetect.SquashTag, CPUstate);
  }
  if (!willCommitNow) {
    return;
  }
  const bool halt = isHeadHalt();
  pop(CPUstate);
  if (halt) {
    CPUstate.ROBModule.haltCommitted = true;
    CPUstate.ROBModule.haltRd = headDest();
  }
}
