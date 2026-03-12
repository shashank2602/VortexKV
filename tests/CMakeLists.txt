add_executable(TestCompactString testCompactString.cpp)
target_link_libraries(TestCompactString PRIVATE RapidKV_Storage gtest_main)
set_target_properties(TestCompactString PROPERTIES FOLDER "Tests")
add_test(NAME TestCompactString COMMAND TestCompactString)

add_executable(TestFlatMap testFlatMap.cpp)
target_link_libraries(TestFlatMap PRIVATE RapidKV_Storage gtest_main)
set_target_properties(TestFlatMap PROPERTIES FOLDER "Tests")
add_test(NAME TestFlatMap COMMAND TestFlatMap)

add_executable(TestDatabase testDatabase.cpp)
target_link_libraries(TestDatabase PRIVATE RapidKV_Storage gtest_main)
set_target_properties(TestDatabase PROPERTIES FOLDER "Tests")
add_test(NAME TestDatabase COMMAND TestDatabase)

add_executable(TestRespProtocol testRespProtocol.cpp)
target_link_libraries(TestRespProtocol PRIVATE RapidKV_Commands gtest_main)
set_target_properties(TestRespProtocol PROPERTIES FOLDER "Tests")
add_test(NAME TestRespProtocol COMMAND TestRespProtocol)

add_executable(TestIntegration testIntegration.cpp)
target_link_libraries(TestIntegration PRIVATE RapidKV_Commands gtest_main)
set_target_properties(TestIntegration PROPERTIES FOLDER "Tests")
add_test(NAME TestIntegration COMMAND TestIntegration)