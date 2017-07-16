cmake_minimum_required(VERSION 2.8.10)

project(tm-wf-irr)

include(CheckCXXCompilerFlag)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
		message(FATAL_ERROR "Too old gcc - ${CMAKE_CXX_COMPILER_VERSION}. Need ≥ 4.8!")
	else() # GCC >= 4.8 detected
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread --std=c++11 -Wall")
	endif()
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
		message(FATAL_ERROR "Too old clang - ${CMAKE_CXX_COMPILER_VERSION}. Need ≥ 3.3!")
	else ()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread --std=c++11 -Wall")
	endif()
else()
	message(FATAL_ERROR "Unknown compiler – edit CMakeLists.txt to add it and ensure C++11 support")
endif()

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -O0")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -O0")

add_library(${PROJECT_NAME}  STATIC  src/tmapi.cpp  src/transaction.cpp  src/variable.cpp)


add_executable(microbench  src/microbenchmark.cpp)
target_link_libraries(
    microbench
    ${PROJECT_NAME}
    boost_program_options
    boost_chrono
    boost_thread
    boost_system
)

add_executable(speed src/speed.cpp)
target_link_libraries(
    speed
    ${PROJECT_NAME}
    boost_program_options
    boost_chrono
    boost_thread
    boost_system
)
