#pragma once
#include "common.h"
#include "module.h"
#include <array>
#include <cstdint>

// Register-based RS entries (flat, no inheritance): the ReservationStation /
// AddressRS hierarchy was retired at the RS Register migration. Widths follow
// the tight-fit discipline: op < 32 (ALU static_assert), phy tag 7b
// (InvalidPhy=0 sentinel, real tags 1..127), robTag 8b (0xFF sentinel domain
// preserved), memIndex 7b (MEM_STORE_BIT + 6b slot).
struct RegOperand {
  Register<7> tag;
  Register<32> imm;
};
struct IntRS {
  Register<1> busy;
  Register<5> op;
  RegOperand src1, src2;
  Register<8> robTag;
};
struct LoadRS {
  Register<1> busy;
  Register<5> op;
  RegOperand src1, src2;
  Register<8> robTag;
  Register<7> memIndex;
};
struct StoreAddrRS {
  Register<1> busy;
  Register<5> op;
  RegOperand src1, src2;
  Register<8> robTag;
  Register<7> memIndex;
};
struct StoreValueRS {
  Register<1> busy;
  RegOperand data;
  Register<8> robTag;
  Register<7> memIndex;
};
struct BranchRS {
  Register<1> busy;
  Register<5> op;
  RegOperand src1, src2;
  Register<8> robTag;
  Register<32> imm;
  Register<32> pc;
};

// --- Input wires: nested groups stay within the reflect 14-member limit ---
// Issue selection: valid + one-of-five type flags + target slots
struct RSInputIssueSel {
  Wire<1> valid;
  Wire<1> hasInteger, hasLoad, hasStore, hasBranch, hasMultiply;
  Wire<4> integerSlot;
  Wire<2> loadSlot;
  Wire<3> storeAddrSlot, storeValueSlot;
  Wire<2> branchSlot;
  Wire<2> multiplySlot; // MULTIPLYRS_CAP == 4
};
// Issue push payloads (flattened from IssuePacket; free is not carried --
// push always allocates). One payload group per RS array.
struct RSInputIntPayload {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
};
struct RSInputLoadPayload {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
  Wire<7> memIndex;
};
struct RSInputStoreAddrPayload {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
  Wire<7> memIndex;
};
struct RSInputStoreValuePayload {
  Wire<7> dataTag;
  Wire<32> dataImm;
  Wire<8> robTag;
  Wire<7> memIndex;
};
struct RSInputBranchPayload {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
  Wire<32> imm, pc;
};
struct RSInputMulPayload {
  Wire<5> op;
  Wire<7> s1Tag, s2Tag;
  Wire<32> s1Imm, s2Imm;
  Wire<8> robTag;
};
struct RSInputIssueData {
  RSInputIntPayload intP;
  RSInputLoadPayload loadP;
  RSInputStoreAddrPayload saP;
  RSInputStoreValuePayload svP;
  RSInputBranchPayload brP;
  RSInputMulPayload mulP;
};
// Dispatch release ports (readiness itself is judged by DispatchArbiter over
// PRF state; RS only needs the granted slot + the AGU load/store split)
struct RSInputDispatch {
  Wire<1> aluValid, aguValid, bruValid, mulValid;
  Wire<4> aluIdx, aguIdx, bruIdx;
  Wire<2> mulIdx; // MULTIPLYRS_CAP == 4
  Wire<1> aguIsLoad;
};
struct RSInputSquash {
  Wire<1> needSquash;
  Wire<8> SquashTag;
};
// storeValue ready-release: an operand whose value now lives in the PRF (or
// is a constant) frees its slot. Computed CPU-side over this RS's own
// committed state (getStoreValueData) + PRF readiness.
struct RSInputPRF {
  std::array<Wire<1>, STORERS_CAP> svReady;
};
struct RSInput {
  RSInputIssueSel sel;
  RSInputIssueData data;
  RSInputDispatch dispatch;
  RSInputSquash squash;
  RSInputPRF prf;
};
struct RSOutput { Wire<1> _unused; };
struct RSInner {
  std::array<IntRS, INTEGERRS_CAP> integerRS;
  std::array<LoadRS, LOADRS_CAP> loadRS;
  std::array<StoreAddrRS, STORERS_CAP> storeAddressRS;
  std::array<StoreValueRS, STORERS_CAP> storeValueRS;
  std::array<BranchRS, BRANCHRS_CAP> branchRS;
  // Dedicated multiply pool (mirror of IntRS: busy/op/srcs/robTag). M-ops
  // never share the integer RS, so a mul-heavy stream cannot starve ALU ops.
  std::array<IntRS, MULTIPLYRS_CAP> multiplyRS;
};
static_assert(MULTIPLYRS_CAP == 4, "multiply slot scans are fixed-length");

struct RSUnit : public dark::Module<RSInput, RSOutput, RSInner> {
public:
  // free-slot priority scans (bridge accessors over committed state; called
  // by the unconverted IssueArbiter in comb)
  int tryAllocInteger() const;
  int tryAllocLoad() const;
  int tryAllocStoreAddress() const;
  int tryAllocStoreValue() const;
  int tryAllocBranch() const;
  // field bridge accessors (Register is non-copyable: consumers read fields,
  // never whole entries)
  bool isIntFree(int i) const {
    return !static_cast<bool>(integerRS[i].busy);
  }
  Operation getIntOp(int i) const {
    return static_cast<Operation>(static_cast<uint32_t>(integerRS[i].op));
  }
  Operand getIntSrc1(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(integerRS[i].src1.tag)),
            static_cast<int32_t>(static_cast<uint32_t>(integerRS[i].src1.imm))};
  }
  Operand getIntSrc2(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(integerRS[i].src2.tag)),
            static_cast<int32_t>(static_cast<uint32_t>(integerRS[i].src2.imm))};
  }
  RobTag getIntRobTag(int i) const {
    return static_cast<RobTag>(static_cast<uint32_t>(integerRS[i].robTag));
  }
  bool isLoadFree(int i) const { return !static_cast<bool>(loadRS[i].busy); }
  Operand getLoadSrc1(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(loadRS[i].src1.tag)),
            static_cast<int32_t>(static_cast<uint32_t>(loadRS[i].src1.imm))};
  }
  Operand getLoadSrc2(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(loadRS[i].src2.tag)),
            static_cast<int32_t>(static_cast<uint32_t>(loadRS[i].src2.imm))};
  }
  RobTag getLoadRobTag(int i) const {
    return static_cast<RobTag>(static_cast<uint32_t>(loadRS[i].robTag));
  }
  uint8_t getLoadMemIndex(int i) const {
    return static_cast<uint8_t>(static_cast<uint32_t>(loadRS[i].memIndex));
  }
  bool isSaFree(int i) const {
    return !static_cast<bool>(storeAddressRS[i].busy);
  }
  Operand getSaSrc1(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(storeAddressRS[i].src1.tag)),
            static_cast<int32_t>(
                static_cast<uint32_t>(storeAddressRS[i].src1.imm))};
  }
  Operand getSaSrc2(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(storeAddressRS[i].src2.tag)),
            static_cast<int32_t>(
                static_cast<uint32_t>(storeAddressRS[i].src2.imm))};
  }
  RobTag getSaRobTag(int i) const {
    return static_cast<RobTag>(static_cast<uint32_t>(storeAddressRS[i].robTag));
  }
  uint8_t getSaMemIndex(int i) const {
    return static_cast<uint8_t>(
        static_cast<uint32_t>(storeAddressRS[i].memIndex));
  }
  bool isSvFree(int i) const {
    return !static_cast<bool>(storeValueRS[i].busy);
  }
  Operand getSvData(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(storeValueRS[i].data.tag)),
            static_cast<int32_t>(
                static_cast<uint32_t>(storeValueRS[i].data.imm))};
  }
  RobTag getSvRobTag(int i) const {
    return static_cast<RobTag>(static_cast<uint32_t>(storeValueRS[i].robTag));
  }
  uint8_t getSvMemIndex(int i) const {
    return static_cast<uint8_t>(
        static_cast<uint32_t>(storeValueRS[i].memIndex));
  }
  bool isBrFree(int i) const {
    return !static_cast<bool>(branchRS[i].busy);
  }
  Operation getBrOp(int i) const {
    return static_cast<Operation>(static_cast<uint32_t>(branchRS[i].op));
  }
  Operand getBrSrc1(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(branchRS[i].src1.tag)),
            static_cast<int32_t>(static_cast<uint32_t>(branchRS[i].src1.imm))};
  }
  Operand getBrSrc2(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(branchRS[i].src2.tag)),
            static_cast<int32_t>(static_cast<uint32_t>(branchRS[i].src2.imm))};
  }
  RobTag getBrRobTag(int i) const {
    return static_cast<RobTag>(static_cast<uint32_t>(branchRS[i].robTag));
  }
  int32_t getBrImm(int i) const {
    return static_cast<int32_t>(static_cast<uint32_t>(branchRS[i].imm));
  }
  uint32_t getBrPc(int i) const {
    return static_cast<uint32_t>(branchRS[i].pc);
  }
  bool isMulFree(int i) const {
    return !static_cast<bool>(multiplyRS[i].busy);
  }
  Operation getMulOp(int i) const {
    return static_cast<Operation>(static_cast<uint32_t>(multiplyRS[i].op));
  }
  Operand getMulSrc1(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(multiplyRS[i].src1.tag)),
            static_cast<int32_t>(static_cast<uint32_t>(multiplyRS[i].src1.imm))};
  }
  Operand getMulSrc2(int i) const {
    return {static_cast<int>(static_cast<uint32_t>(multiplyRS[i].src2.tag)),
            static_cast<int32_t>(static_cast<uint32_t>(multiplyRS[i].src2.imm))};
  }
  RobTag getMulRobTag(int i) const {
    return static_cast<RobTag>(static_cast<uint32_t>(multiplyRS[i].robTag));
  }
  void work() override;
};
