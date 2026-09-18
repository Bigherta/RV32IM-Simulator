#include "../include/RS.hpp"
#include "ROB.hpp"
#include <cstdint>

// Per-slot single-write-point structure (RTL discipline: one driver per
// Register per cycle, no early returns):
//   pushHit  - issue allocates this slot (mutually exclusive with
//              release/flush: issue only selects slots that are free in the
//              committed state, release/flush require occupied)
//   relHit   - DispatchArbiter granted this slot to an execution unit (only
//              entries older than the squash point are granted, so relHit and
//              flushHit are mutually exclusive; merged anyway -- both write
//              the same freed state)
//   flushHit - squash retires this younger-than-squash-point entry
// storeValueRS is the one true overlap: its ready-release has no tag guard,
// so release and flush converge through the same else-if (both set free=1).

void RSUnit::work() {
  const bool issueValid = static_cast<bool>(sel.valid);
  const bool hasInt = issueValid && static_cast<bool>(sel.hasInteger);
  const bool hasLoad = issueValid && static_cast<bool>(sel.hasLoad);
  const bool hasStore = issueValid && static_cast<bool>(sel.hasStore);
  const bool hasBranch = issueValid && static_cast<bool>(sel.hasBranch);
  const bool hasMul = issueValid && static_cast<bool>(sel.hasMultiply);
  const bool hasDiv = issueValid && static_cast<bool>(sel.hasDivide);
  const uint32_t intSlot = static_cast<uint32_t>(sel.integerSlot);
  const uint32_t loadSlotV = static_cast<uint32_t>(sel.loadSlot);
  const uint32_t saSlot = static_cast<uint32_t>(sel.storeAddrSlot);
  const uint32_t svSlot = static_cast<uint32_t>(sel.storeValueSlot);
  const uint32_t brSlot = static_cast<uint32_t>(sel.branchSlot);
  const uint32_t mulSlot = static_cast<uint32_t>(sel.multiplySlot);
  const uint32_t divSlotV = static_cast<uint32_t>(sel.divideSlot);

  const bool needSquash = static_cast<bool>(squash.needSquash);
  const uint32_t sqTag = static_cast<uint32_t>(squash.SquashTag);

  const bool aluRel = static_cast<bool>(dispatch.aluValid);
  const uint32_t aluIdx = static_cast<uint32_t>(dispatch.aluIdx);
  const bool aguRel = static_cast<bool>(dispatch.aguValid);
  const uint32_t aguIdx = static_cast<uint32_t>(dispatch.aguIdx);
  const bool aguIsLoadV = static_cast<bool>(dispatch.aguIsLoad);
  const bool bruRel = static_cast<bool>(dispatch.bruValid);
  const uint32_t bruIdx = static_cast<uint32_t>(dispatch.bruIdx);
  const bool mulRel = static_cast<bool>(dispatch.mulValid);
  const uint32_t mulIdx = static_cast<uint32_t>(dispatch.mulIdx);
  const bool divRel = static_cast<bool>(dispatch.divValid);
  const uint32_t divIdx = static_cast<uint32_t>(dispatch.divIdx);

  // ---- integerRS: push / release / flush ----
  for (int i = 0; i < INTEGERRS_CAP; ++i) {
    const bool busyOld = static_cast<bool>(integerRS[i].busy);
    const uint32_t tagOld = static_cast<uint32_t>(integerRS[i].robTag);
    const bool pushHit = hasInt && intSlot == static_cast<uint32_t>(i);
    const bool relHit = aluRel && aluIdx == static_cast<uint32_t>(i);
    const bool flushHit =
        needSquash && busyOld && ROB::isOlder(sqTag, tagOld);
    if (pushHit) {
      integerRS[i].busy <= 1;
      integerRS[i].op <= static_cast<uint32_t>(data.intP.op);
      integerRS[i].src1.tag <= static_cast<uint32_t>(data.intP.s1Tag);
      integerRS[i].src1.imm <= static_cast<uint32_t>(data.intP.s1Imm);
      integerRS[i].src2.tag <= static_cast<uint32_t>(data.intP.s2Tag);
      integerRS[i].src2.imm <= static_cast<uint32_t>(data.intP.s2Imm);
      integerRS[i].robTag <= static_cast<uint32_t>(data.intP.robTag);
    } else if (relHit || flushHit) {
      integerRS[i].busy <= 0;
      integerRS[i].src1.tag <= 0;
      integerRS[i].src1.imm <= 0;
      integerRS[i].src2.tag <= 0;
      integerRS[i].src2.imm <= 0;
    }
  }

  // ---- loadRS: push / release / flush ----
  for (int i = 0; i < LOADRS_CAP; ++i) {
    const bool busyOld = static_cast<bool>(loadRS[i].busy);
    const uint32_t tagOld = static_cast<uint32_t>(loadRS[i].robTag);
    const bool pushHit = hasLoad && loadSlotV == static_cast<uint32_t>(i);
    const bool relHit = aguRel && aguIsLoadV && aguIdx == static_cast<uint32_t>(i);
    const bool flushHit =
        needSquash && busyOld && ROB::isOlder(sqTag, tagOld);
    if (pushHit) {
      loadRS[i].busy <= 1;
      loadRS[i].op <= static_cast<uint32_t>(data.loadP.op);
      loadRS[i].src1.tag <= static_cast<uint32_t>(data.loadP.s1Tag);
      loadRS[i].src1.imm <= static_cast<uint32_t>(data.loadP.s1Imm);
      loadRS[i].src2.tag <= static_cast<uint32_t>(data.loadP.s2Tag);
      loadRS[i].src2.imm <= static_cast<uint32_t>(data.loadP.s2Imm);
      loadRS[i].robTag <= static_cast<uint32_t>(data.loadP.robTag);
      loadRS[i].memIndex <= static_cast<uint32_t>(data.loadP.memIndex);
    } else if (relHit || flushHit) {
      loadRS[i].busy <= 0;
      loadRS[i].src1.tag <= 0;
      loadRS[i].src1.imm <= 0;
      loadRS[i].src2.tag <= 0;
      loadRS[i].src2.imm <= 0;
    }
  }

  // ---- storeAddressRS: push / release / flush (release/flush clear only
  // src1 -- reference semantics; src2 is rewritten on the next push) ----
  for (int i = 0; i < STORERS_CAP; ++i) {
    const bool busyOld = static_cast<bool>(storeAddressRS[i].busy);
    const uint32_t tagOld = static_cast<uint32_t>(storeAddressRS[i].robTag);
    const bool pushHit = hasStore && saSlot == static_cast<uint32_t>(i);
    const bool relHit =
        aguRel && !aguIsLoadV && aguIdx == static_cast<uint32_t>(i);
    const bool flushHit =
        needSquash && busyOld && ROB::isOlder(sqTag, tagOld);
    if (pushHit) {
      storeAddressRS[i].busy <= 1;
      storeAddressRS[i].op <= static_cast<uint32_t>(data.saP.op);
      storeAddressRS[i].src1.tag <= static_cast<uint32_t>(data.saP.s1Tag);
      storeAddressRS[i].src1.imm <= static_cast<uint32_t>(data.saP.s1Imm);
      storeAddressRS[i].src2.tag <= static_cast<uint32_t>(data.saP.s2Tag);
      storeAddressRS[i].src2.imm <= static_cast<uint32_t>(data.saP.s2Imm);
      storeAddressRS[i].robTag <= static_cast<uint32_t>(data.saP.robTag);
      storeAddressRS[i].memIndex <= static_cast<uint32_t>(data.saP.memIndex);
    } else if (relHit || flushHit) {
      storeAddressRS[i].busy <= 0;
      storeAddressRS[i].src1.tag <= 0;
      storeAddressRS[i].src1.imm <= 0;
    }
  }

  // ---- storeValueRS: push / ready-release / flush (the one real overlap:
  // ready-release has no tag guard in the reference, so release and flush
  // converge on the same freed state) ----
  for (int i = 0; i < STORERS_CAP; ++i) {
    const bool busyOld = static_cast<bool>(storeValueRS[i].busy);
    const uint32_t tagOld = static_cast<uint32_t>(storeValueRS[i].robTag);
    const bool pushHit = hasStore && svSlot == static_cast<uint32_t>(i);
    const bool relHit = busyOld && static_cast<bool>(prf.svReady[i]);
    const bool flushHit =
        needSquash && busyOld && ROB::isOlder(sqTag, tagOld);
    if (pushHit) {
      storeValueRS[i].busy <= 1;
      storeValueRS[i].data.tag <= static_cast<uint32_t>(data.svP.dataTag);
      storeValueRS[i].data.imm <= static_cast<uint32_t>(data.svP.dataImm);
      storeValueRS[i].robTag <= static_cast<uint32_t>(data.svP.robTag);
      storeValueRS[i].memIndex <= static_cast<uint32_t>(data.svP.memIndex);
    } else if (relHit || flushHit) {
      storeValueRS[i].busy <= 0;
    }
  }

  // ---- branchRS: push / release / flush ----
  for (int i = 0; i < BRANCHRS_CAP; ++i) {
    const bool busyOld = static_cast<bool>(branchRS[i].busy);
    const uint32_t tagOld = static_cast<uint32_t>(branchRS[i].robTag);
    const bool pushHit = hasBranch && brSlot == static_cast<uint32_t>(i);
    const bool relHit = bruRel && bruIdx == static_cast<uint32_t>(i);
    const bool flushHit =
        needSquash && busyOld && ROB::isOlder(sqTag, tagOld);
    if (pushHit) {
      branchRS[i].busy <= 1;
      branchRS[i].op <= static_cast<uint32_t>(data.brP.op);
      branchRS[i].src1.tag <= static_cast<uint32_t>(data.brP.s1Tag);
      branchRS[i].src1.imm <= static_cast<uint32_t>(data.brP.s1Imm);
      branchRS[i].src2.tag <= static_cast<uint32_t>(data.brP.s2Tag);
      branchRS[i].src2.imm <= static_cast<uint32_t>(data.brP.s2Imm);
      branchRS[i].robTag <= static_cast<uint32_t>(data.brP.robTag);
      branchRS[i].imm <= static_cast<uint32_t>(data.brP.imm);
      branchRS[i].pc <= static_cast<uint32_t>(data.brP.pc);
    } else if (relHit || flushHit) {
      branchRS[i].busy <= 0;
      branchRS[i].src1.tag <= 0;
      branchRS[i].src1.imm <= 0;
      branchRS[i].src2.tag <= 0;
      branchRS[i].src2.imm <= 0;
    }
  }

  // ---- multiplyRS: push / release / flush (same shape as integerRS) ----
  for (int i = 0; i < MULTIPLYRS_CAP; ++i) {
    const bool busyOld = static_cast<bool>(multiplyRS[i].busy);
    const uint32_t tagOld = static_cast<uint32_t>(multiplyRS[i].robTag);
    const bool pushHit = hasMul && mulSlot == static_cast<uint32_t>(i);
    const bool relHit = mulRel && mulIdx == static_cast<uint32_t>(i);
    const bool flushHit =
        needSquash && busyOld && ROB::isOlder(sqTag, tagOld);
    if (pushHit) {
      multiplyRS[i].busy <= 1;
      multiplyRS[i].op <= static_cast<uint32_t>(data.mulP.op);
      multiplyRS[i].src1.tag <= static_cast<uint32_t>(data.mulP.s1Tag);
      multiplyRS[i].src1.imm <= static_cast<uint32_t>(data.mulP.s1Imm);
      multiplyRS[i].src2.tag <= static_cast<uint32_t>(data.mulP.s2Tag);
      multiplyRS[i].src2.imm <= static_cast<uint32_t>(data.mulP.s2Imm);
      multiplyRS[i].robTag <= static_cast<uint32_t>(data.mulP.robTag);
    } else if (relHit || flushHit) {
      multiplyRS[i].busy <= 0;
      multiplyRS[i].src1.tag <= 0;
      multiplyRS[i].src1.imm <= 0;
      multiplyRS[i].src2.tag <= 0;
      multiplyRS[i].src2.imm <= 0;
    }
  }

  // ---- divideRS: push / release / flush (same shape as multiplyRS; the
  // release port is the DispatchArbiter's div grant, which the arbiter only
  // raises while DIV::canAccept() holds) ----
  for (int i = 0; i < DIVIDERS_CAP; ++i) {
    const bool busyOld = static_cast<bool>(divideRS[i].busy);
    const uint32_t tagOld = static_cast<uint32_t>(divideRS[i].robTag);
    const bool pushHit = hasDiv && divSlotV == static_cast<uint32_t>(i);
    const bool relHit = divRel && divIdx == static_cast<uint32_t>(i);
    const bool flushHit =
        needSquash && busyOld && ROB::isOlder(sqTag, tagOld);
    if (pushHit) {
      divideRS[i].busy <= 1;
      divideRS[i].op <= static_cast<uint32_t>(data.divP.op);
      divideRS[i].src1.tag <= static_cast<uint32_t>(data.divP.s1Tag);
      divideRS[i].src1.imm <= static_cast<uint32_t>(data.divP.s1Imm);
      divideRS[i].src2.tag <= static_cast<uint32_t>(data.divP.s2Tag);
      divideRS[i].src2.imm <= static_cast<uint32_t>(data.divP.s2Imm);
      divideRS[i].robTag <= static_cast<uint32_t>(data.divP.robTag);
    } else if (relHit || flushHit) {
      divideRS[i].busy <= 0;
      divideRS[i].src1.tag <= 0;
      divideRS[i].src1.imm <= 0;
      divideRS[i].src2.tag <= 0;
      divideRS[i].src2.imm <= 0;
    }
  }
}
