cmake_minimum_required(VERSION 3.20)

project(qalsh-installed-consumer LANGUAGES CXX)

find_package(qalsh CONFIG REQUIRED)

enable_testing()
add_executable(qalsh_installed_consumer main.cc)
target_compile_features(qalsh_installed_consumer PRIVATE cxx_std_20)
target_link_libraries(qalsh_installed_consumer PRIVATE qalsh::qalsh)
add_test(NAME qalsh_installed_consumer_query COMMAND qalsh_installed_consumer)
