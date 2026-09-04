cmake_minimum_required(VERSION 3.16.3)

project(openfhe_ckks_rns_exporter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Pin the API used by export_ckks_vectors.cpp. Pass OpenFHE_DIR on the CMake
# command line when OpenFHE is installed outside the default prefix.
find_package(OpenFHE 1.5.1 EXACT CONFIG REQUIRED)

message(STATUS "OpenFHE version: ${BASE_OPENFHE_VERSION}")
message(STATUS "OpenFHE include directory: ${OpenFHE_INCLUDE}")
message(STATUS "OpenFHE library directory: ${OpenFHE_LIBDIR}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenFHE_CXX_FLAGS}")

add_executable(openfhe_export_ckks export_ckks_vectors.cpp)
target_include_directories(openfhe_export_ckks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${OPENMP_INCLUDES}
    ${OpenFHE_INCLUDE}
    ${OpenFHE_INCLUDE}/third-party/include
    ${OpenFHE_INCLUDE}/core
    ${OpenFHE_INCLUDE}/pke
    ${OpenFHE_INCLUDE}/binfhe)
target_link_directories(openfhe_export_ckks PRIVATE
    ${OpenFHE_LIBDIR}
    ${OPENMP_LIBRARIES})
target_link_libraries(openfhe_export_ckks PRIVATE
    ${OpenFHE_SHARED_LIBRARIES})

if(UNIX AND NOT APPLE)
    target_link_options(openfhe_export_ckks PRIVATE -Wl,--no-as-needed)
endif()
