cmake_minimum_required(VERSION 3.10)
project(CPU_Simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CPU_SOURCES
    src/CPU/CPU.cpp
    src/Decoder/Decoder.cpp
    src/DMEM/DMEM.cpp
    src/DCache/DCache.cpp
    src/ROB/ROB.cpp
    src/RS/RS.cpp
    src/ALU/ALU.cpp
    src/AGU/AGU.cpp
    src/MUL/MUL.cpp
    src/DIV/DIV.cpp
    src/BRU/BRU.cpp
    src/DynamicArbiter/DynamicArbiter.cpp
    src/StaticArbiter/StaticArbiter.cpp
    src/CDB/CDB.cpp
    src/IMEM/IMEM.cpp
    src/ICache/ICache.cpp
    src/FetchUnit/FetchUnit.cpp
    src/LQ/LQ.cpp
    src/SQ/SQ.cpp
    src/RAT/RAT.cpp
    src/InstructBuffer/InstructBuffer.cpp
    src/BPU/BPU.cpp
    src/PRF/PRF.cpp
)

add_executable(code
    src/main/main.cpp
    ${CPU_SOURCES}
)

target_include_directories(code PRIVATE src/include)

set_target_properties(code PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}
)