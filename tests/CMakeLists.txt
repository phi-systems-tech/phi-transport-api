# jsontext.h is Qt-free, so this target does not link Qt either.
add_executable(transport_jsontext_tests jsontext_tests.cpp)
target_compile_features(transport_jsontext_tests PRIVATE cxx_std_20)
target_include_directories(transport_jsontext_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME transport_jsontext_tests COMMAND transport_jsontext_tests)

# LogEntry, Error and Scalar are Qt-free too, so neither test target links Qt.
# That is the point of the exercise: if a test of the contract needed Qt, so
# would an implementation of it.
add_executable(transport_types_tests types_tests.cpp)
target_compile_features(transport_types_tests PRIVATE cxx_std_20)
target_include_directories(transport_types_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME transport_types_tests COMMAND transport_types_tests)

# The envelope helpers are Qt-free as well, so this target links nothing either.
# It pins the wire shapes the transports used to each own a copy of.
add_executable(transport_envelope_tests envelope_tests.cpp)
target_compile_features(transport_envelope_tests PRIVATE cxx_std_20)
target_include_directories(transport_envelope_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME transport_envelope_tests COMMAND transport_envelope_tests)

# Routing lives in TransportPluginBase, which is plugin-side and Qt-free, so it
# can be exercised against a stub facade without a transport or a core. Since
# 2.0.0 the contract carries the runtime loop, so this one links phi::runtime.
add_executable(transport_dispatch_tests dispatch_tests.cpp)
target_compile_features(transport_dispatch_tests PRIVATE cxx_std_20)
target_include_directories(transport_dispatch_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(transport_dispatch_tests PRIVATE phi::runtime)
add_test(NAME transport_dispatch_tests COMMAND transport_dispatch_tests)

add_executable(transport_management_tests management_tests.cpp)
target_include_directories(transport_management_tests PRIVATE ${CMAKE_SOURCE_DIR}/include/phi/transport/api)
target_link_libraries(transport_management_tests PRIVATE phi_transport_api)
add_test(NAME transport_management_tests COMMAND transport_management_tests)
