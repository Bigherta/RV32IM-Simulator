#include "AXI.hpp"
#include "CPU.hpp"
#include <cassert>

AXIBusPorts AXIBus::ports(const AXIBusInput &in) const {
  AXIBusPorts out{};
  const bool credit = count < 64;
  const bool data = grantHeld ? heldData : in.data.arValid && (!in.fetch.arValid || preferData);
  out.slave = in.data;
  out.slave.arValid = credit && (in.fetch.arValid || in.data.arValid);
  out.slave.arAddr = data ? in.data.arAddr : in.fetch.arAddr;
  out.slave.rReady = count != 0 && (owner[head] ? in.data.rReady : in.fetch.rReady);
  out.fetch = out.data = in.slave;
  out.fetch.arReady = credit && !data && in.slave.arReady;
  out.data.arReady = credit && data && in.slave.arReady;
  out.fetch.rValid = in.slave.rValid && count != 0 && !owner[head];
  out.data.rValid = in.slave.rValid && count != 0 && owner[head];
  return out;
}

void AXIBus::tick(const AXIBusInput &in, systemState &next) const {
  const auto out = ports(in);
  const bool push = out.slave.arValid && in.slave.arReady;
  const bool pop = out.slave.rReady && in.slave.rValid;
  if (out.slave.arValid && !in.slave.arReady && !grantHeld) {
    next.AXIBusModule.grantHeld = true;
    next.AXIBusModule.heldData = in.data.arValid && (!in.fetch.arValid || preferData);
  } else if (push) next.AXIBusModule.grantHeld = false;
  if (push) next.AXIBusModule.owner[(head + count) & 63] = out.data.arReady;
  if (pop) next.AXIBusModule.head = (head + 1) & 63;
  next.AXIBusModule.count = count + unsigned(push) - unsigned(pop);
  if (push && in.fetch.arValid && in.data.arValid)
    next.AXIBusModule.preferData = !preferData;
}

unsigned AXIFetch::occupancy() const {
  unsigned n = 0;
  for (const auto &line : lines) n += unsigned(line.valid);
  return n;
}
int AXIFetch::unscheduled() const {
  int found = -1;
  for (int age = 0; age < IMEM_CAP; ++age) {
    const int s = (head + age) & (IMEM_CAP - 1);
    if (found < 0 && lines[s].valid && lines[s].issued < ICACHE_WORDS_PER_LINE) found = s;
  }
  return found;
}
int AXIFetch::collecting() const {
  int found = -1;
  for (int age = 0; age < IMEM_CAP; ++age) {
    const int s = (head + age) & (IMEM_CAP - 1);
    if (found < 0 && lines[s].valid && lines[s].collected < lines[s].issued) found = s;
  }
  return found;
}
LineReturn AXIFetch::getReturn() const {
  const auto &line = lines[head];
  return {line.valid && line.collected == ICACHE_WORDS_PER_LINE, line.address, line.words};
}
AXIMaster AXIFetch::port() const {
  AXIMaster out{};
  const int s = unscheduled();
  out.arValid = pending || s >= 0;
  out.arAddr = pending ? pendingAddr : s >= 0 ? lines[s].address + (lines[s].issued << 2) : 0;
  out.rReady = true;
  return out;
}
void AXIFetch::tick(const AXIFetchInput &in, systemState &next) const {
  const auto out = port();
  const bool ar = out.arValid && in.bus.arReady;
  const bool r = in.bus.rValid;
  const unsigned infl = inflight + unsigned(ar) - unsigned(r);
  assert(!r || inflight != 0);
  next.AXIFetchModule.inflight = infl;
  if (in.squash) {
    for (int i = 0; i < IMEM_CAP; ++i) next.AXIFetchModule.lines[i].valid = false;
    next.AXIFetchModule.head = 0;
    next.AXIFetchModule.discard = infl;
    if (pending && ar) next.AXIFetchModule.pending = false;
    else if (!pending && !ar && out.arValid) {
      next.AXIFetchModule.pending = true;
      next.AXIFetchModule.pendingAddr = out.arAddr;
    }
  } else {
    unsigned disc = discard;
    if (r && discard != 0) --disc;
    else if (r) {
      const int s = collecting();
      assert(s >= 0);
      next.AXIFetchModule.lines[s].words[lines[s].collected] = in.bus.rData;
      next.AXIFetchModule.lines[s].collected = lines[s].collected + 1;
    }
    if (ar && pending) { next.AXIFetchModule.pending = false; ++disc; }
    else if (ar) {
      const int s = unscheduled();
      next.AXIFetchModule.lines[s].issued = lines[s].issued + 1;
    }
    next.AXIFetchModule.discard = disc;
    const unsigned occ = occupancy();
    const bool push = in.fetchValid && (occ < IMEM_CAP || in.lineConsumed);
    const int tail = (head + occ) & (IMEM_CAP - 1);
    if (in.lineConsumed && !(push && tail == head)) next.AXIFetchModule.lines[head].valid = false;
    if (push) {
      next.AXIFetchModule.lines[tail].valid = true;
      next.AXIFetchModule.lines[tail].address = in.fetchPC;
      next.AXIFetchModule.lines[tail].issued = 0;
      next.AXIFetchModule.lines[tail].collected = 0;
    }
    if (in.lineConsumed) next.AXIFetchModule.head = (head + 1) & (IMEM_CAP - 1);
  }
}

int AXIData::readIssue() const {
  int found = -1;
  for (int age = 0; age < AXI_DEPTH; ++age) {
    const int s = (rh + age) & (AXI_DEPTH - 1);
    if (age < rn && found < 0 && reads[s].issued < DCACHE_WORDS_PER_LINE) found = s;
  }
  return found;
}
int AXIData::writeIssue(bool address) const {
  int found = -1;
  for (int age = 0; age < AXI_DEPTH; ++age) {
    const int s = (wh + age) & (AXI_DEPTH - 1);
    if (age < wn && found < 0 && (address ? writes[s].aw : writes[s].w) <
        (writes[s].mmio ? 1 : DCACHE_WORDS_PER_LINE)) found = s;
  }
  return found;
}
bool AXIData::isReplyReady() const { return rn != 0 && reads[rh].collected == DCACHE_WORDS_PER_LINE; }
bool AXIData::isWriteReplyReady() const {
  return wn != 0 && writes[wh].collected == (writes[wh].mmio ? 1 : DCACHE_WORDS_PER_LINE);
}
MemReply AXIData::reply() const {
  MemReply out{};
  out.address = reads[rh].address;
  out.transaction = reads[rh].transaction;
  for (int i = 0; i < DCACHE_BLOCK_CAP; ++i) out.lineData[i] = reads[rh].bytes[i];
  return out;
}
MemWriteReply AXIData::writeReply() const { return {writes[wh].address, writes[wh].transaction}; }
AXIMaster AXIData::port() const {
  AXIMaster out{};
  const int rd = readIssue(), aw = writeIssue(true), wd = writeIssue(false);
  out.arValid = rd >= 0;
  if (rd >= 0) out.arAddr = reads[rd].address + (reads[rd].issued << 2);
  out.awValid = aw >= 0;
  if (aw >= 0) out.awAddr = writes[aw].address + (writes[aw].aw << 2);
  out.wValid = wd >= 0;
  if (wd >= 0) {
    out.wStrb = 15;
    for (int b = 0; b < 4; ++b) out.wData |= uint32_t(writes[wd].bytes[(writes[wd].w << 2) + b]) << (b << 3);
  }
  out.rReady = out.bReady = true;
  return out;
}
void AXIData::tick(const AXIDataInput &in, systemState &next) const {
  const auto out = port();
  const int rd = readIssue(), aw = writeIssue(true), wd = writeIssue(false);
  const bool pushW = in.request.writeValid || in.exitValid;
  const bool pushR = in.request.readValid;
  assert(!pushW || canAcceptWrite());
  assert(!pushR || canAcceptRead(pushW));
  assert(!in.exitValid || (!in.request.writeValid && exitReady()));
  if (out.arValid && in.bus.arReady) next.AXIDataModule.reads[rd].issued = reads[rd].issued + 1;
  if (out.awValid && in.bus.awReady) next.AXIDataModule.writes[aw].aw = writes[aw].aw + 1;
  if (out.wValid && in.bus.wReady) next.AXIDataModule.writes[wd].w = writes[wd].w + 1;
  if (in.bus.rValid) {
    int s = -1;
    for (int age = 0; age < AXI_DEPTH; ++age) {
      const int i = (rh + age) & (AXI_DEPTH - 1);
      if (age < rn && s < 0 && reads[i].collected < reads[i].issued) s = i;
    }
    assert(s >= 0 && in.bus.rResp == 0);
    for (int b = 0; b < 4; ++b)
      next.AXIDataModule.reads[s].bytes[(reads[s].collected << 2) + b] = in.bus.rData >> (b << 3);
    next.AXIDataModule.reads[s].collected = reads[s].collected + 1;
  }
  if (in.bus.bValid) {
    int s = -1;
    for (int age = 0; age < AXI_DEPTH; ++age) {
      const int i = (wh + age) & (AXI_DEPTH - 1);
      if (age < wn && s < 0 && writes[i].collected < writes[i].aw && writes[i].collected < writes[i].w) s = i;
    }
    assert(s >= 0 && in.bus.bResp == 0);
    next.AXIDataModule.writes[s].collected = writes[s].collected + 1;
  }
  if (pushR) {
    const int s = (rh + rn) & (AXI_DEPTH - 1);
    next.AXIDataModule.reads[s].address = in.request.read.address;
    next.AXIDataModule.reads[s].transaction = in.request.read.transaction;
    next.AXIDataModule.reads[s].issued = 0;
    next.AXIDataModule.reads[s].collected = 0;
  }
  if (pushW) {
    const int s = (wh + wn) & (AXI_DEPTH - 1);
    next.AXIDataModule.writes[s].address = in.exitValid ? AXI_EXIT_ADDR : in.request.write.address;
    next.AXIDataModule.writes[s].transaction = in.request.write.transaction;
    next.AXIDataModule.writes[s].aw = 0;
    next.AXIDataModule.writes[s].w = 0;
    next.AXIDataModule.writes[s].collected = 0;
    next.AXIDataModule.writes[s].mmio = in.exitValid;
    for (int b = 0; b < DCACHE_BLOCK_CAP; ++b)
      next.AXIDataModule.writes[s].bytes[b] = in.exitValid
          ? (b < 4 ? in.exitValue >> ((b & 3) << 3) : 0) : in.request.write.lineData[b];
  }
  if (in.replyConsumed) { assert(isReplyReady()); next.AXIDataModule.rh = (rh + 1) & 15; }
  if (in.writeConsumed) { assert(isWriteReplyReady()); next.AXIDataModule.wh = (wh + 1) & 15; }
  next.AXIDataModule.rn = rn + unsigned(pushR) - unsigned(in.replyConsumed);
  next.AXIDataModule.wn = wn + unsigned(pushW) - unsigned(in.writeConsumed);
}

Slave::Slave(const Memory &mem) : ram(AXI_RAM_BYTES, 0) {
  for (uint32_t i = 0; i < MEM_SIZE; ++i) ram[i] = mem.read_data(i);
}
void Slave::snapshotFrom(const Slave &s) {
  ar = s.ar; aw = s.aw; w = s.w; r = s.r; b = s.b;
  ah = s.ah; an = s.an; wh = s.wh; wn = s.wn; dh = s.dh; dn = s.dn;
  rh = s.rh; rn = s.rn; bh = s.bh; bn = s.bn;
  cycle = s.cycle; result = s.result; done = s.done;
  random = s.random;
  for (int byte = 0; byte < 4; ++byte)
    sampledRead[byte] = an && response(ar[ah]) == 0 ? s.ram[ar[ah] + byte] : 0;
}
uint8_t Slave::response(uint32_t addr) {
  return (addr & 3u) ? 2 : uint64_t(addr) + 4 > AXI_RAM_BYTES ? 3 : 0;
}
AXISlavePort Slave::port() const {
  return {an < AXI_DEPTH, rn != 0 && r[rh].due <= cycle, wn < AXI_DEPTH,
          dn < AXI_DEPTH, bn != 0 && b[bh].due <= cycle,
          rn ? r[rh].data : 0, uint8_t(rn ? r[rh].resp : 0), uint8_t(bn ? b[bh].resp : 0)};
}
void Slave::tick(const AXIMaster &in, systemState &next) const {
  const auto out = port();
  const bool rf = out.rValid && in.rReady, bf = out.bValid && in.bReady;
  const bool af = in.arValid && out.arReady, wf = in.awValid && out.awReady, df = in.wValid && out.wReady;
  const bool sr = an != 0 && rn - unsigned(rf) < AXI_DEPTH;
  const bool sw = wn != 0 && dn != 0 && bn - unsigned(bf) < AXI_DEPTH;
  uint32_t rng = random;
  if (sr && sw) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; }
  const bool writeFirst = sr && sw && (rng & 1u);
  if (sr && sw) next.SlaveModule.random = rng;
  // RAM is host storage, sampled before tick in CPU::comb. Write/read service
  // ordering matches the course slave: a simultaneous write precedes a read.
  if (af) next.SlaveModule.ar[(ah + an) & 15] = in.arAddr;
  if (wf) next.SlaveModule.aw[(wh + wn) & 15] = in.awAddr;
  if (df) next.SlaveModule.w[(dh + dn) & 15] = {in.wData, in.wStrb};
  if (sw) {
    const bool exit = aw[wh] == AXI_EXIT_ADDR && w[dh].strb == 15;
    const uint8_t resp = aw[wh] == AXI_EXIT_ADDR ? (exit ? 0 : 2) : response(aw[wh]);
    next.SlaveModule.b[(bh + bn) & 15] = {w[dh].data, cycle + 1 + AXI_LATENCY, resp, exit};
    if (resp == 0 && !exit)
      for (int byte = 0; byte < 4; ++byte)
        if (w[dh].strb & (1u << byte)) next.SlaveModule.ram[aw[wh] + byte] = w[dh].data >> (byte << 3);
  }
  if (sr) {
    uint32_t data = 0;
    const uint8_t resp = response(ar[ah]);
    if (resp == 0) for (int byte = 0; byte < 4; ++byte) {
      uint8_t value = sampledRead[byte];
      if (writeFirst && response(aw[wh]) == 0 && ar[ah] + byte >= aw[wh] && ar[ah] + byte - aw[wh] < 4 &&
          (w[dh].strb & (1u << (ar[ah] + byte - aw[wh]))))
        value = w[dh].data >> ((ar[ah] + byte - aw[wh]) << 3);
      data |= uint32_t(value) << (byte << 3);
    }
    next.SlaveModule.r[(rh + rn) & 15] = {data, cycle + 1 + AXI_LATENCY, resp};
  }
  next.SlaveModule.ah = (ah + unsigned(sr)) & 15; next.SlaveModule.an = an + unsigned(af) - unsigned(sr);
  next.SlaveModule.wh = (wh + unsigned(sw)) & 15; next.SlaveModule.wn = wn + unsigned(wf) - unsigned(sw);
  next.SlaveModule.dh = (dh + unsigned(sw)) & 15; next.SlaveModule.dn = dn + unsigned(df) - unsigned(sw);
  next.SlaveModule.rh = (rh + unsigned(rf)) & 15; next.SlaveModule.rn = rn + unsigned(sr) - unsigned(rf);
  next.SlaveModule.bh = (bh + unsigned(bf)) & 15; next.SlaveModule.bn = bn + unsigned(sw) - unsigned(bf);
  next.SlaveModule.cycle = cycle + 1;
  if (bf && b[bh].exit) { next.SlaveModule.done = true; next.SlaveModule.result = b[bh].result; }
}
