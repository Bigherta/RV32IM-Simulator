#include "../include/PRF.hpp"
#include "ROB.hpp"
#include "../include/util.hpp"
#include <cassert>
#include <cstdint>
void PRF::work() {
  // ---- bootDone reset: single-cycle init, overlaps cycle 0 (ROB empty => no
  // issue/squash) ----
  if (!static_cast<bool>(bootDone)) {
    for (int i = 0; i < REGISTER_CAP; ++i)
      PhysicalRegs[i].ready <= true;
    for (int i = 0; i < PRF_CAP - REGISTER_CAP; ++i)
      freeList[i] <= static_cast<uint32_t>(REGISTER_CAP + i);
    tailSeq <= static_cast<uint32_t>(PRF_CAP - REGISTER_CAP);
    headSeq <= static_cast<uint32_t>(0);
    bootDone <= true;
  }
  // Cache frequently used Wire values as local combinational signals
  bool needSquash = static_cast<bool>(squash.needSquash);
  uint32_t squashTag = static_cast<uint32_t>(squash.SquashTag);
  uint8_t ckptId = static_cast<uint32_t>(squash.CkptId);
  uint32_t robHeadVal = static_cast<uint32_t>(rob.robHead);
  bool isRobEmpty = static_cast<bool>(rob.isRobEmpty);
  bool isHeadReady = static_cast<bool>(rob.isRobHeadCommitReady);
  bool issueValid = static_cast<bool>(issue.issueValid);
  bool issueAlloc = issueValid && static_cast<bool>(issue.issueAllocDest);
  uint8_t issuePhyVal = static_cast<uint32_t>(issue.issuePhy);
  uint8_t issueCkpt = static_cast<uint32_t>(issue.issueCkptId);
  bool issueIsCtrl = static_cast<bool>(issue.issueIsControl);
  uint32_t issuePCVal = static_cast<uint32_t>(issue.issuePC);

  // ---- Triple-CDB writeback: three independent write ports
  // (aluCDB / lqCDB / mulCDB). Register renaming guarantees a distinct
  // newPhy per in-flight instruction, so same-cycle writebacks always hit
  // different physical registers (no WAW; the Register array's per-element
  // single write port is naturally satisfied). Each port keeps its own
  // squash guard, verbatim from the single-CDB era.
  bool cdbWriteAlu = false;
  uint32_t cdbPhyAlu = 0;
  uint32_t cdbValAlu = 0;
  if (static_cast<bool>(cdbOfALU.cdbValid)) {
    uint32_t cdbTag = static_cast<uint32_t>(cdbOfALU.cdbRobTag);
    if (!needSquash || ROB::isOlder(cdbTag, squashTag)) {
      if (!static_cast<bool>(cdbOfALU.cdbIsControl)) {
        uint32_t newPhy = static_cast<uint32_t>(cdbOfALU.cdbNewPhy);
        if (newPhy != static_cast<uint32_t>(InvalidPhy)) {
          cdbWriteAlu = true;
          cdbPhyAlu = newPhy;
          cdbValAlu = static_cast<uint32_t>(cdbOfALU.cdbValue);
        }
      }
    }
  }
  bool cdbWriteLq = false;
  uint32_t cdbPhyLq = 0;
  uint32_t cdbValLq = 0;
  if (static_cast<bool>(cdbOfLQ.cdbValid)) {
    uint32_t cdbTag = static_cast<uint32_t>(cdbOfLQ.cdbRobTag);
    if (!needSquash || ROB::isOlder(cdbTag, squashTag)) {
      uint32_t newPhy = static_cast<uint32_t>(cdbOfLQ.cdbNewPhy);
      if (newPhy != static_cast<uint32_t>(InvalidPhy)) {
        cdbWriteLq = true;
        cdbPhyLq = newPhy;
        cdbValLq = static_cast<uint32_t>(cdbOfLQ.cdbValue);
      }
    }
  }
  bool cdbWriteMul = false;
  uint32_t cdbPhyMul = 0;
  uint32_t cdbValMul = 0;
  uint32_t cdbTagMul = 0;
  if (static_cast<bool>(cdbOfMUL.cdbValid)) {
    uint32_t cdbTag = static_cast<uint32_t>(cdbOfMUL.cdbRobTag);
    if (!needSquash || ROB::isOlder(cdbTag, squashTag)) {
      uint32_t newPhy = static_cast<uint32_t>(cdbOfMUL.cdbNewPhy);
      if (newPhy != static_cast<uint32_t>(InvalidPhy)) {
        cdbWriteMul = true;
        cdbPhyMul = newPhy;
        cdbValMul = static_cast<uint32_t>(cdbOfMUL.cdbValue);
        cdbTagMul = cdbTag;
      }
    }
  }

  if (cdbWriteAlu) {
    if (debug::enabled(debug::TOPIC_PRF))
      debug::print("PRF write P%d = %d (aluCDB)\n", cdbPhyAlu, cdbValAlu);
    PhysicalRegs[cdbPhyAlu].ready <= true;
    PhysicalRegs[cdbPhyAlu].value <= cdbValAlu;
  }
  if (cdbWriteLq) {
    if (debug::enabled(debug::TOPIC_PRF))
      debug::print("PRF write P%d = %d (lqCDB)\n", cdbPhyLq, cdbValLq);
    PhysicalRegs[cdbPhyLq].ready <= true;
    PhysicalRegs[cdbPhyLq].value <= cdbValLq;
  }
  if (cdbWriteMul) {
    if (debug::enabled(debug::TOPIC_EXEC))
      debug::print("prf mul-write rob=%u phy=%d val=%08x\n", cdbTagMul,
                   cdbPhyMul, cdbValMul);
    PhysicalRegs[cdbPhyMul].ready <= true;
    PhysicalRegs[cdbPhyMul].value <= cdbValMul;
  }

  // ---- Issue: PRFHeadCkpt snapshot + free-list pop ----
  // Single-write-point for headSeq: compute next value, apply once at end.
  uint32_t curHead = static_cast<uint32_t>(headSeq);
  uint32_t nextHead = curHead;
  bool doPop = false;
  uint8_t popPhy = 0;
  if (issueValid) {
    uint32_t headSnap = curHead + (issueAlloc ? 1 : 0);
    PRFHeadCkpt[issueCkpt] <= headSnap;
    if (issueAlloc) {
      popPhy = static_cast<uint32_t>(freeList[curHead & (PRF_CAP - 1)]);
      assert(popPhy != InvalidPhy);
      assert(popPhy == issuePhyVal);
      nextHead = curHead + 1;
      doPop = true;
    }
  }

  // ---- Pop side effects (ready/link value): identical under squash or not,
  // applied once here (reference tick does pop before the squash check) ----
  if (doPop) {
    if (issueIsCtrl) {
      if (debug::enabled(debug::TOPIC_PRF))
        debug::print("PRF link P%d = %d (pc+4)\n", issuePhyVal, issuePCVal + 4);
      PhysicalRegs[issuePhyVal].ready <= true;
      PhysicalRegs[issuePhyVal].value <= issuePCVal + 4;
    } else {
      PhysicalRegs[issuePhyVal].ready <= false;
    }
  }

  // ---- Squash: single-write-point for headSeq, handles PRFHeadCkpt hazard
  // ----
  if (needSquash) {
      uint32_t ckptVal;
      if (issueValid && ckptId == issueCkpt) {
        // Same-cycle write-read hazard: use newly computed headSnap
        ckptVal = curHead + (issueAlloc ? 1 : 0);
      } else {
        ckptVal = static_cast<uint32_t>(PRFHeadCkpt[ckptId]);
      }
      assert(static_cast<uint32_t>(tailSeq) - ckptVal <=
             static_cast<uint32_t>(PRF_CAP));
      nextHead = ckptVal;
    // doPop implies nextHead != curHead; restore overwrites nextHead
    if (nextHead != curHead)
      headSeq <= nextHead;
  } else {
    if (doPop)
      headSeq <= nextHead;

    // ---- Commit: push oldPhy ----
    bool doReturn = false;
    if (isRobEmpty || !isHeadReady)
      doReturn = true;
    if (static_cast<bool>(rob.robHeadIsHalt))
      doReturn = true;
    uint32_t hType = static_cast<uint32_t>(rob.robHeadType);
    if (hType != static_cast<uint32_t>(ROBType::REGISTER) &&
        hType != static_cast<uint32_t>(ROBType::LINK))
      doReturn = true;
    if (!doReturn) {
      uint32_t oldPhy = static_cast<uint32_t>(rob.robHeadOldPhy);
      if (oldPhy != static_cast<uint32_t>(InvalidPhy)) {
        uint32_t tail = static_cast<uint32_t>(tailSeq);
        freeList[tail & (PRF_CAP - 1)] <= oldPhy;
        tailSeq <= tail + 1;
      }
    }
  }
}
