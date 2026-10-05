#include "../include/ROB.hpp"
#include "../include/CPU.hpp"
#include "../include/util.hpp"
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
  return !isEmpty() && slot < ROB_CAP && ROBqueue[slot].tag == tag;
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
  CPUstate.ROBModule.head = robNextTag(head);
}

bool ROB::isCommitReadyAt(int index) const {
  return ROBqueue[index].isCommitReady;
}

ROBType ROB::getType(int index) const { return ROBqueue[index].type; }

int32_t ROB::getPC(int index) const { return ROBqueue[index].pc; }

bool ROB::isHalt(int index) const { return ROBqueue[index].halt; }

uint8_t ROB::getCkptId(int index) const { return ROBqueue[index].ckptId; }

void ROB::setROBCommitReady(RobTag tag, bool issued, systemState &CPUstate) const {
  const auto slot = robSlot(tag);
  if (slot >= ROB_CAP || (isEmpty() && !issued))
    return;
  const auto rowTag = issued && slot == robSlot(next_tag) ? next_tag : ROBqueue[slot].tag;
  if (rowTag == tag) CPUstate.ROBModule.ROBqueue[slot].isCommitReady = true;
}

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
  if (input.issuePacket.valid) {
    push(input.issuePacket.robEntry, CPUstate);
  }
  // BRU set ROB ready
  if (!input.BRUModule.isEmpty()) {
    uint8_t brRobTag = input.BRUModule.headRobTag();
    if (!input.squashDetect.needSquash ||
        (input.squashDetect.needSquash &&
         ROB::isOlder(brRobTag, input.squashDetect.SquashTag))) {
      setROBCommitReady(brRobTag, input.issuePacket.valid, CPUstate);
    }
  }
  // SQ set ROB ready (stores)
  auto sqHead = input.SQModule.getHead();
  for (int k = 0; k < MEMQ_SCAN_WINDOW; ++k) {
    uint8_t i = (sqHead + k) & SQ_MASK;
    if (!input.SQModule.isActive(i))
      continue;
    if (input.SQModule.isReadyToCommit(i) && !input.SQModule.isCommitted(i)) {
      auto sqTag = input.SQModule.getRobTag(i);
      if (!input.squashDetect.needSquash ||
          (input.squashDetect.needSquash &&
           ROB::isOlder(sqTag, input.squashDetect.SquashTag))) {
        setROBCommitReady(sqTag, input.issuePacket.valid, CPUstate);
      }
    }
  }
  // CDB set ROB ready (dual)
  if (input.cdbOfALU.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfALU.robTag, input.squashDetect.SquashTag)) {
      setROBCommitReady(input.cdbOfALU.robTag, input.issuePacket.valid, CPUstate);
    }
  }
  if (input.cdbOfLQ.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfLQ.robTag, input.squashDetect.SquashTag)) {
      setROBCommitReady(input.cdbOfLQ.robTag, input.issuePacket.valid, CPUstate);
    }
  }
  if (input.cdbOfMul.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfMul.robTag, input.squashDetect.SquashTag)) {
      if (matchesTag(input.cdbOfMul.robTag)) {
        setROBCommitReady(input.cdbOfMul.robTag, input.issuePacket.valid, CPUstate);
        if (debug::enabled(debug::TOPIC_EXEC))
          debug::print("rob mul-ready rob=%u\n", input.cdbOfMul.robTag);
      }
    }
  }
  if (input.cdbOfDiv.valid) {
    if (!input.squashDetect.needSquash ||
        ROB::isOlder(input.cdbOfDiv.robTag, input.squashDetect.SquashTag)) {
      if (matchesTag(input.cdbOfDiv.robTag)) {
        setROBCommitReady(input.cdbOfDiv.robTag, input.issuePacket.valid, CPUstate);
        if (debug::enabled(debug::TOPIC_EXEC))
          debug::print("rob div-ready rob=%u\n", input.cdbOfDiv.robTag);
      }
    }
  }
  // ROB squash
  if (input.squashDetect.needSquash) {
    flush(input.squashDetect.SquashTag, CPUstate);
  }
  if (!willCommitNow) {
    return;
  }
  const bool halt = isHeadHalt();
  if (debug::enabled(debug::TOPIC_EXEC))
    debug::print("rob commit rob=%u halt=%d\n", getHead(), halt ? 1 : 0);
  pop(CPUstate);
  if (halt) {
    CPUstate.ROBModule.haltCommitted = true;
    CPUstate.ROBModule.haltRd = headDest();
  }
}
