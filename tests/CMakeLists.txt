add_executable(crepl_unit_tests unit.cpp)
target_link_libraries(crepl_unit_tests PRIVATE crepl_core)
add_test(NAME frontend_unit COMMAND crepl_unit_tests)
add_library(crepl_fixture SHARED fixture.cpp)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME incremental_integration COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/integration.py $<TARGET_FILE:crepl> $<TARGET_FILE:crepl_fixture>)
set_tests_properties(incremental_integration PROPERTIES TIMEOUT 120)
