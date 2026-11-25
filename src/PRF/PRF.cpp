#include "../include/PRF.hpp"
#include "../include/CPU.hpp"
#include "common.hpp"
#include <cassert>
#include <cstring>

PRF::PRF() {
  for (int i = 0; i < PRF_CAP; ++i) {
    PhysicalRegs[i].ready = false;
  }
  // P1-P31 are bound to x1-x31 at reset (RAT[x] = Px, committed init value 0).
  // x0 is never renamed (all issue paths skip rd==0): RAT[0] stays
  // InvalidPhy(=0) and P0 is permanently reserved -- it never enters the free
  // list, which makes InvalidPhy=0 a valid "no register" sentinel everywhere.
  for (int i = 0; i < REGISTER_CAP; ++i)
    PhysicalRegs[i].ready = true;
  // P32..P(PRF_CAP-1) enter the free list; empty tail slots stay InvalidPhy.
  memset(freeList, 0, sizeof(freeList));
  for (int i = REGISTER_CAP; i < PRF_CAP; ++i) {
    freeList[prfSlot(tailSeq)] = i;
    tailSeq = prfSeqNext(tailSeq);
  }
}
void PRF::push(int index, PrfSeq &writeTail, systemState &CPUstate) const {
  assert(index != InvalidPhy); // P0-dead invariant: only real tags are recycled
  assert(prfSeqDistance(headSeq, writeTail) <
         PRF_CAP); // free list never overflows
  CPUstate.PRFModule.freeList[prfSlot(writeTail)] = index;
  writeTail = prfSeqNext(writeTail);
  CPUstate.PRFModule.tailSeq = writeTail;
}

bool PRF::isFreeListEmpty() const { return headSeq == tailSeq; }

PrfSeq PRF::getHeadSeq() const { return headSeq; }

bool PRF::isReady(int index) const { return index == 0 || PhysicalRegs[index].ready; }
int32_t PRF::getValue(int index) const {
  uint32_t value = 0;
  if (index != 0) {
    if (PhysicalRegs[index].everWritten) value = payload[index - 1].readLane(0);
    for (const auto &port : priorWrites)
      if (port.valid && port.phy == index) value = port.value;
  }
  return static_cast<int32_t>(value);
}

void PRF::advanceReplayTickets(const PRFInput &input, systemState &CPUstate) const {
  if (input.squashDetect.needSquash) {
    // The flush edge still sees the original live ROB window.
    const RobTag head = input.ROBModule.getHead();
    const unsigned boundary = (input.squashDetect.SquashTag - head) & ROB_TAG_MASK;
    const unsigned live = (input.ROBModule.getNextTag() - head) & ROB_TAG_MASK;
    assert(!input.recovery.active && boundary < live && live <= ROB_CAP);
    for (unsigned slot = 0; slot < ROB_CAP; ++slot)
      CPUstate.PRFModule.recycleWindow[slot] = slot > boundary && slot < live;
    RobTag tag = head;
    for (unsigned lane = 0; lane < RECOVERY_WIDTH; ++lane) {
      CPUstate.PRFModule.recycleTags[lane] = tag;
      tag = robNextTag(tag);
    }
  } else if (input.recovery.active) {
    for (unsigned slot = 0; slot < ROB_CAP; ++slot)
      CPUstate.PRFModule.recycleWindow[slot] =
          slot + RECOVERY_WIDTH < ROB_CAP && recycleWindow[slot + RECOVERY_WIDTH];
    for (unsigned lane = 0; lane < RECOVERY_WIDTH; ++lane)
      CPUstate.PRFModule.recycleTags[lane] =
          RobTag((recycleTags[lane] + RECOVERY_WIDTH) & ROB_TAG_MASK);
  }
}

void PRF::tick(const PRFInput &input, systemState &CPUstate) {
  std::array<PRFWriteRecord, PRF_VALUE_WRITERS> writes{};
  std::array<bool, PRF_VALUE_WRITERS> ready{};
  for (unsigned lane = 0; lane < ISSUE_WIDTH; ++lane) {
    const auto &p = input.issuePacket.lanes[lane];
    if (!input.freeze && !input.squashDetect.needSquash && !input.recovery.active &&
        lane < input.issuePacket.count && p.allocDest && p.phy != InvalidPhy) {
      writes[lane] = {true, static_cast<uint8_t>(p.phy), p.pc + 4u};
      ready[lane] = p.isControl;
    }
  }
  for (unsigned port = 0; port < ALU_CDB_PORTS; ++port) {
    const auto &cdb = port == 0 ? input.cdbOfALU : input.cdbOfALUSecond;
    if (cdb.valid && input.ROBModule.matchesTag(cdb.robTag) && !cdb.isControl &&
        !input.freeze && (!input.squashDetect.needSquash || ROB::isOlder(cdb.robTag, input.squashDetect.SquashTag))) {
      const auto phy = input.ROBModule.getNewPhy(robSlot(cdb.robTag));
      writes[ISSUE_WIDTH + port] = {phy != InvalidPhy, phy, uint32_t(cdb.value)};
      ready[ISSUE_WIDTH + port] = true;
    }
  }
  // Load follows MUL/DIV in WB priority and has its own previous-edge record.
  auto record = [&](unsigned port, const auto &cdb) {
    if (!input.freeze && cdb.valid && input.ROBModule.matchesTag(cdb.robTag) &&
        (!input.squashDetect.needSquash ||
         ROB::isOlder(cdb.robTag, input.squashDetect.SquashTag))) {
      const int newPhy = input.ROBModule.getNewPhy(robSlot(cdb.robTag));
      if (newPhy != InvalidPhy) {
        writes[ISSUE_WIDTH + port] = {true, uint8_t(newPhy), uint32_t(cdb.value)};
        ready[ISSUE_WIDTH + port] = true;
      }
    }
  };
  record(MULDIV_CDB_PORT, input.cdbOfShared);
  record(LOAD_CDB_PORT, input.cdbOfLoad);
  // One 1RW SRAM per real physical register. All idle banks read every edge,
  // including freeze; previous-edge writes bridge the unavailable SRAM rdata.
  for (unsigned writer = 0; writer < PRF_VALUE_WRITERS; ++writer) {
    CPUstate.PRFModule.priorWrites[writer].valid = writes[writer].valid;
    CPUstate.PRFModule.priorWrites[writer].phy = writes[writer].phy;
    CPUstate.PRFModule.priorWrites[writer].value = writes[writer].value;
  }
  for (int bank = 0; bank < PRF_CAP - 1; ++bank) {
    SRAM<1, 32>::Input port{};
    port.enable = true;
    port.writeMask[0] = true;
    bool resultReady = false;
    for (unsigned writer = 0; writer < PRF_VALUE_WRITERS; ++writer) {
      if (writes[writer].valid && writes[writer].phy == bank + 1) {
        port.writeEnable = true;
        port.writeData[0] = writes[writer].value;
        resultReady = ready[writer];
      }
    }
    payload[bank].tick(port, CPUstate.PRFModule.payload[bank]);
    if (port.writeEnable) {
      CPUstate.PRFModule.PhysicalRegs[bank + 1].ready = resultReady;
      CPUstate.PRFModule.PhysicalRegs[bank + 1].everWritten = true;
    }
  }
  if (input.freeze) return;
  if constexpr (useReplayTickets) advanceReplayTickets(input, CPUstate);
  PrfSeq writeTail = tailSeq; // Multiple recycle writes share one local cursor.
  // Squash recycles young destinations without clearing their payloads.
  if (input.recovery.active) {
    RobTag tag = input.recovery.nextTag;
    for (unsigned lane = 0; lane < RECOVERY_WIDTH; ++lane) {
      const auto &entry = input.replay[lane];
      const auto original = [&] {
        const unsigned offset = unsigned(input.recovery.offset) + lane;
        return offset < ROB_CAP && offset < input.recovery.liveCount &&
               entry.tag == tag && ROB::isYounger(tag, input.recovery.squashTag);
      };
      bool eligible;
      if constexpr (useReplayTickets) {
        eligible = recycleWindow[lane] && entry.tag == recycleTags[lane];
        assert(recycleTags[lane] == tag && eligible == original());
      } else {
        eligible = original();
      }
      if (eligible && entry.newPhy != InvalidPhy) push(entry.newPhy, writeTail, CPUstate);
      tag = robNextTag(tag);
    }
  } else if (!input.squashDetect.needSquash) {
    // 6. if not, distribute a free PRF to the logic register
    PrfSeq allocatedHead = headSeq;
    for (unsigned lane = 0; lane < ISSUE_WIDTH; ++lane) {
      const auto &p = input.issuePacket.lanes[lane];
      if (lane < input.issuePacket.count && p.allocDest) {
        const auto phy = freeList[prfSlot(allocatedHead)];
        assert(phy != InvalidPhy && phy == p.phy);
        allocatedHead = prfSeqNext(allocatedHead);
      }
    }
    if (allocatedHead != headSeq) CPUstate.PRFModule.headSeq = allocatedHead;
  }
  // 7. release the old physical register
  for (unsigned lane = 0; lane < COMMIT_WIDTH; ++lane) {
    if (lane < input.commits.count) {
      int headIdx = robSlot(input.commits.tags[lane]);
      if (!input.ROBModule.isHalt(headIdx) &&
          (input.ROBModule.getType(headIdx) == ROBType::REGISTER ||
           input.ROBModule.getType(headIdx) == ROBType::LINK)) {
        int oldPhy = input.ROBModule.getOldPhy(headIdx);
        if (oldPhy != InvalidPhy) push(oldPhy, writeTail, CPUstate);
      }
    }
  }
}
