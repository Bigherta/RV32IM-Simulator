#include "../include/CDB.hpp"
#include "../include/ROB.hpp"
#include "../include/util.hpp"
#include <cstdint>

bool AluCDB::aluLive() const {
  return !static_cast<bool>(aluEmpty) &&
         (!static_cast<bool>(squashNeed) ||
          ROB::isOlder(static_cast<uint32_t>(aluRobTag),
                       static_cast<uint32_t>(squashTag)));
}

void AluCDB::wire_output() {
  valid = [this]() -> uint32_t { return aluLive() ? 1u : 0u; };
  value = [this]() -> uint32_t {
    return aluLive() ? static_cast<uint32_t>(aluValue) : 0u;
  };
  robTag = [this]() -> uint32_t {
    return aluLive() ? static_cast<uint32_t>(aluRobTag) : 0u;
  };
  isControl = [this]() -> uint32_t {
    return aluLive() ? static_cast<uint32_t>(aluIsControl) : 0u;
  };
}

bool MulCDB::mulLive() const {
  return !static_cast<bool>(mulEmpty) &&
         (!static_cast<bool>(squashNeed) ||
          ROB::isOlder(static_cast<uint32_t>(mulRobTag),
                       static_cast<uint32_t>(squashTag)));
}

void MulCDB::wire_output() {
  // The valid lambda is the single broadcast event per cycle (lazy wires
  // cache per cycle, so this prints at most once): mirrors the main tree's
  // mulCDB::build print for the documented VERBOSE=exec counting method.
  valid = [this]() -> uint32_t {
    if (!mulLive())
      return 0u;
    if (debug::enabled(debug::TOPIC_EXEC))
      debug::print("mul cdb broadcast rob=%u val=%08x\n",
                   static_cast<uint32_t>(mulRobTag),
                   static_cast<uint32_t>(mulValue));
    return 1u;
  };
  value = [this]() -> uint32_t {
    return mulLive() ? static_cast<uint32_t>(mulValue) : 0u;
  };
  robTag = [this]() -> uint32_t {
    return mulLive() ? static_cast<uint32_t>(mulRobTag) : 0u;
  };
}


bool DivCDB::divLive() const {
  return !static_cast<bool>(divEmpty) &&
         (!static_cast<bool>(squashNeed) ||
          ROB::isOlder(static_cast<uint32_t>(divRobTag),
                       static_cast<uint32_t>(squashTag)));
}

// No VERBOSE=exec print here: the reference's divCDB::build has none (unlike
// mulCDB::build), so both trees keep identical exec-topic output.
void DivCDB::wire_output() {
  valid = [this]() -> uint32_t { return divLive() ? 1u : 0u; };
  value = [this]() -> uint32_t {
    return divLive() ? static_cast<uint32_t>(divValue) : 0u;
  };
  robTag = [this]() -> uint32_t {
    return divLive() ? static_cast<uint32_t>(divRobTag) : 0u;
  };
}

bool LqCDB::lsqLive() const {
  return static_cast<bool>(lsqValid) &&
         (!static_cast<bool>(squashNeed) ||
          ROB::isOlder(static_cast<uint32_t>(lsqRobTag),
                       static_cast<uint32_t>(squashTag)));
}

void LqCDB::wire_output() {
  valid = [this]() -> uint32_t { return lsqLive() ? 1u : 0u; };
  value = [this]() -> uint32_t {
    return lsqLive() ? static_cast<uint32_t>(lsqValue) : 0u;
  };
  robTag = [this]() -> uint32_t {
    return lsqLive() ? static_cast<uint32_t>(lsqRobTag) : 0u;
  };
  memIndex = [this]() -> uint32_t {
    return lsqLive() ? static_cast<uint32_t>(lsqMemIndex) : 0u;
  };
}
