#pragma once
#include "tools.h"
struct AluCDBInput {
  Wire<1> aluEmpty;     // ALUModule.isEmpty()
  Wire<32> aluValue;    // ALUModule.headValue()
  Wire<7> aluRobTag;    // ALUModule.headRobTag()
  Wire<1> aluIsControl; // ALUModule.headIsControl()
  Wire<1> squashNeed;   // squashDetect.needSquash
  Wire<7> squashTag;    // squashDetect.SquashTag
};
struct AluCDBOutput {
  Wire<1> valid;
  Wire<32> value;
  Wire<7> robTag;
  Wire<1> isControl;
};
struct AluCDB : dark::Module<AluCDBInput, AluCDBOutput> {
  AluCDB() { wire_output(); }
  void work() override {} // pure combinational: outputs are lazy Wires
private:
  void wire_output();
  // ALU head candidate surviving the squash guard (verbatim from CDBArbiter).
  bool aluLive() const;
};
struct MulCDBInput {
  Wire<1> mulEmpty;    
  Wire<32> mulValue;
  Wire<7> mulRobTag; 
  Wire<1> squashNeed;   // squashDetect.needSquash
  Wire<7> squashTag;    // squashDetect.SquashTag
};
struct MulCDBOutput {
  Wire<1> valid;
  Wire<32> value;
  Wire<7> robTag;
};
struct MulCDB : dark::Module<MulCDBInput, MulCDBOutput> {
  MulCDB() { wire_output(); }
  void work() override {} // pure combinational: outputs are lazy Wires
private:
  void wire_output();
  bool mulLive() const;
};
struct LqCDBInput {
  Wire<1> lsqValid;     // LQModule.CDBDetect() != -1
  Wire<7> lsqMemIndex;  // CDBDetect() hit ? LQ index : 0
  Wire<7> lsqRobTag;    // hit ? LQModule.getRobTag(idx) : 0
  Wire<32> lsqValue;    // hit ? LQModule.getValue(idx) : 0 (gated here so the
                        // invalid index never reaches getValue's throw)
  Wire<1> squashNeed;   // squashDetect.needSquash
  Wire<7> squashTag;    // squashDetect.SquashTag
};
struct LqCDBOutput {
  Wire<1> valid;
  Wire<32> value;
  Wire<7> robTag;
  Wire<7> memIndex;
};
struct LqCDB : dark::Module<LqCDBInput, LqCDBOutput> {
  LqCDB() { wire_output(); }
  void work() override {} // pure combinational: outputs are lazy Wires
private:
  void wire_output();
  // LSQ load candidate surviving the squash guard (verbatim from CDBArbiter).
  bool lsqLive() const;
};
