cmake_minimum_required(VERSION 3.10)
project(CPU_ReorderTest LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(reorder_test
	reorder_test.cpp
	../src/CPU/CPU.cpp
	../src/Decoder/Decoder.cpp
	../src/DCache/DCache.cpp
	../src/DMEM/DMEM.cpp
	../src/ROB/ROB.cpp
	../src/RS/RS.cpp
	../src/ALU/ALU.cpp
	../src/MUL/MUL.cpp
	../src/DIV/DIV.cpp
	../src/AGU/AGU.cpp
	../src/BRU/BRU.cpp
	../src/StaticArbiter/StaticArbiter.cpp
	../src/CDB/CDB.cpp
	../src/DynamicArbiter/DynamicArbiter.cpp
	../src/IMEM/IMEM.cpp
	../src/ICache/ICache.cpp
	../src/FetchUnit/FetchUnit.cpp
	../src/LQ/LQ.cpp
	../src/SQ/SQ.cpp
	../src/RAT/RAT.cpp
	../src/InstructBuffer/InstructBuffer.cpp
	../src/BPU/BPU.cpp
	../src/PRF/PRF.cpp
)

target_include_directories(reorder_test PRIVATE ../src/include ../include)

set_target_properties(reorder_test PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
