#pragma once
#ifndef RS_HPP
#define RS_HPP
#include "common.hpp"
#include <cstdint>
struct ReservationStation {
  Operation op;
  bool free;
  Operand src1;   // source operand 1: phy tag (or imm constant when tag==InvalidPhy)
  Operand src2;   // source operand 2
  RobTag robTag = 0xFF;
  ReservationStation() : free(true) {}
  ReservationStation(Operation type) : op(type), free(true) {}
};

struct AddressRS : public ReservationStation {
  uint8_t memIndex = 0;
  AddressRS() : ReservationStation() {}
  AddressRS(Operation type) : ReservationStation(type) {}
};

struct LoadAddressRS : public AddressRS {
  LoadAddressRS() : AddressRS() {}
};

struct StoreAddressRS : public AddressRS {
  StoreAddressRS() : AddressRS() {}
};

struct StoreValueReservationStation {
  bool free;
  Operand data;   // store data source: phy tag (or imm constant when tag==InvalidPhy)
  RobTag robTag = 0xFF;
  uint8_t memIndex = 0;
  StoreValueReservationStation() : free(true) {}
};

struct BranchReservationStation : public ReservationStation {
  int32_t imm;
  uint32_t pc;
  BranchReservationStation() : ReservationStation() {}
  BranchReservationStation(Operation type) : ReservationStation(type) {}
};
struct PRF;
struct IssuePacket;
struct systemState;
struct RSInput {
  SquashInfo squashDetect;
  DispatchBus dispatchBus;
  const IssuePacket &issuePacket;
  const PRF &PRFModule;
  RSInput(const IssuePacket &pkt, const PRF &prf)
      : issuePacket(pkt), PRFModule(prf) {}
};
class RSUnit {
private:
  friend struct ReorderTester;

public:
  ReservationStation integerRS[INTEGERRS_CAP];
  ReservationStation multiplyRS[MULTIPLYRS_CAP];
  LoadAddressRS loadRS[LOADRS_CAP];
  StoreAddressRS storeAddressRS[STORERS_CAP];
  StoreValueReservationStation storeValueRS[STORERS_CAP];
  BranchReservationStation branchRS[BRANCHRS_CAP];
  int tryAllocInteger() const;
  int tryAllocMultiply() const;
  int tryAllocLoad() const;
  int tryAllocStoreAddress() const;
  int tryAllocStoreValue() const;
  int tryAllocBranch() const;
  void tick(const RSInput &, systemState &);
};
#endif // RS_HPP
