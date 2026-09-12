cmake_minimum_required(VERSION 3.20)

project(qalsh-lib VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(QALSH_BUILD_TESTS "Build qalsh-lib tests" ON)

add_library(qalsh STATIC src/qalsh.cc)
add_library(qalsh::qalsh ALIAS qalsh)

target_compile_features(qalsh PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(qalsh PUBLIC Threads::Threads)
target_include_directories(qalsh
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_options(qalsh PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
)

# Use SIMD reductions when the host toolchain provides the OpenMP SIMD
# front-end, without making the library depend on the OpenMP runtime.
find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_compile_definitions(qalsh PRIVATE QALSH_USE_OPENMP_SIMD)
    target_compile_options(qalsh PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fopenmp-simd>
    )
endif()

set_target_properties(qalsh PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(QALSH_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(qalsh_public_test tests/public_test.cc)
        target_link_libraries(qalsh_public_test PRIVATE qalsh::qalsh)
        add_test(NAME qalsh_public_test COMMAND qalsh_public_test)
        add_executable(qalsh_dispatch_test tests/dispatch_test.cc)
        target_link_libraries(qalsh_dispatch_test PRIVATE qalsh::qalsh)
        add_test(NAME qalsh_dispatch_test COMMAND qalsh_dispatch_test ${CMAKE_CURRENT_BINARY_DIR})
        add_executable(qalsh_distance_test tests/distance_test.cc)
        target_link_libraries(qalsh_distance_test PRIVATE qalsh::qalsh)
        add_test(NAME qalsh_distance_test COMMAND qalsh_distance_test)
        add_executable(qalsh_build_test tests/build_test.cc)
        target_link_libraries(qalsh_build_test PRIVATE qalsh::qalsh)
        add_test(NAME qalsh_build_test COMMAND qalsh_build_test ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()

install(TARGETS qalsh
    EXPORT qalshTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT qalshTargets
    FILE qalshTargets.cmake
    NAMESPACE qalsh::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qalsh
)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/qalshConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qalshConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qalsh
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qalshConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qalshConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qalshConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qalsh
)

export(EXPORT qalshTargets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/qalshTargets.cmake
    NAMESPACE qalsh::
)
