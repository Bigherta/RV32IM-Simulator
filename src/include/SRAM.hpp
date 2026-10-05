#pragma once
#include "util.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// sram_fakeram 的主树模型：单端口同步读写，addr 为字索引。
// tick() 只在宿主时序阶段调用一次；读结果在该时钟沿后可用。
// 写粒度受 32 位 lane 限制；禁用/写操作不会产生新的有效读结果。

template <std::size_t DEPTH, std::size_t WIDTH, std::size_t WRITE_GRANULARITY = WIDTH>
class SRAM {
  static_assert(DEPTH >= 1 && DEPTH <= 1048576 && WIDTH >= 1 && WIDTH <= 4096 &&
                    DEPTH <= (std::size_t{1} << 24) / WIDTH,
                "sram_fakeram: invalid depth, width or capacity");
  static_assert(WRITE_GRANULARITY >= 1 && WRITE_GRANULARITY <= 32 &&
                    WRITE_GRANULARITY <= WIDTH && WIDTH % WRITE_GRANULARITY == 0,
                "sram_fakeram: invalid write granularity");

public:
  static constexpr std::size_t Lanes = WIDTH / WRITE_GRANULARITY;
  static constexpr uint32_t LaneMask = 0xFFFFFFFFu >> (32 - WRITE_GRANULARITY);

  // lane 0 对应 wdata 的最低 WRITE_GRANULARITY 位。
  struct Input {
    bool enable = false;
    bool writeEnable = false;
    uint32_t addr = 0;
    std::array<bool, Lanes> writeMask{};
    std::array<uint32_t, Lanes> writeData{};
  };

private:
  using Row = std::array<uint32_t, Lanes>;
  std::array<Row, DEPTH> words{};
  Row readData{};

  [[noreturn]] static void reportAddressError(uint32_t index) {
    debug::print("RAM library error: sram_fakeram address %u outside DEPTH=%llu\n",
                 index, static_cast<unsigned long long>(DEPTH));
    std::exit(EXIT_FAILURE);
  }

public:
  void tick(const Input &input, SRAM &output) const {
    if (input.enable && input.addr >= DEPTH) {
      reportAddressError(input.addr);
    }
    if (input.enable && !input.writeEnable) {
      output.readData = words[input.addr];
    } else if (input.enable) {
      for (std::size_t lane = 0; lane < Lanes; ++lane) {
        if (input.writeMask[lane]) {
          output.words[input.addr][lane] = input.writeData[lane] & LaneMask;
        }
      }
    }
  }

  uint32_t readLane(std::size_t lane) const {
    return readData[lane];
  }
};
