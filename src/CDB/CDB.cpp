#include "../include/CDB.hpp"
#include "../include/ALU.hpp"
#include "../include/MUL.hpp"
#include "../include/DIV.hpp"
#include "../include/LQ.hpp"
#include "../include/ROB.hpp"
#include "../include/common.hpp"
#include "../include/util.hpp"
#include <cstdint>

aluCDB aluCDB::build(const ALU &alu, const SquashInfo &squash) {
  aluCDB alucdb{};
  if (!alu.isEmpty()) {
    uint8_t tag = alu.headRobTag();
    if (!squash.needSquash || ROB::isOlder(tag, squash.SquashTag)) {
      alucdb.valid = true;
      alucdb.value = static_cast<int32_t>(alu.headValue());
      alucdb.robTag = tag;
      alucdb.isControl = alu.headIsControl();
    }
  }
  return alucdb;
}
mulCDB mulCDB::build(const MUL &mul, const SquashInfo &squash) {
  mulCDB mulcdb{};
  if (!mul.isEmpty()) {
    uint8_t tag = mul.headRobTag();
    if (!squash.needSquash || ROB::isOlder(tag, squash.SquashTag)) {
      mulcdb.valid = true;
      mulcdb.value = static_cast<int32_t>(mul.headValue());
      mulcdb.robTag = tag;
      if (debug::enabled(debug::TOPIC_EXEC))
        debug::print("mul cdb broadcast rob=%u val=%08x\n", tag,
                     (uint32_t)mulcdb.value);
    }
  }
  return mulcdb;
}
divCDB divCDB::build(const DIV &div, const SquashInfo &squash) {
  divCDB divcdb{};
  if (div.isReady()) {
    uint8_t tag = div.getResultRobtag();
    if (!squash.needSquash || ROB::isOlder(tag, squash.SquashTag)) {
      divcdb.valid = true;
      divcdb.value = static_cast<int32_t>(div.getValue());
      divcdb.robTag = tag;
    }
  }
  return divcdb;
}
lqCDB lqCDB::build(const LQ &lq, const SquashInfo &squash) {
  lqCDB lqcdb{};
  auto lsqCDBDetect = lq.CDBDetect();
  if (lsqCDBDetect != -1) {
    uint8_t tag = lq.getRobTag(lsqCDBDetect);
    if (!squash.needSquash || ROB::isOlder(tag, squash.SquashTag)) {
      lqcdb.valid = true;
      lqcdb.memIndex = static_cast<uint8_t>(lsqCDBDetect);
      lqcdb.robTag = tag;
      lqcdb.value = lq.getValue(lsqCDBDetect);
    }
  }
  return lqcdb;
}
