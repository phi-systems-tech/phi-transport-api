cmake_minimum_required(VERSION 3.21)

project(phi-transport-api
    VERSION 1.0.0
    DESCRIPTION "Public header-only transport API for Phi"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(phi_transport_api INTERFACE)
add_library(phicore::transport-api ALIAS phi_transport_api)

set_target_properties(phi_transport_api PROPERTIES
    EXPORT_NAME transport-api
)

target_compile_features(phi_transport_api INTERFACE cxx_std_20)

target_link_libraries(phi_transport_api
    INTERFACE
        Qt6::Core
)

target_include_directories(phi_transport_api
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/phi/transport/api>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/phi/transport/api>
)

install(
    DIRECTORY include/phi/transport/api/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phi/transport/api
    FILES_MATCHING PATTERN "*.h"
)

install(
    TARGETS phi_transport_api
    EXPORT phi-transport-apiTargets
)

install(
    EXPORT phi-transport-apiTargets
    FILE phi-transport-apiTargets.cmake
    NAMESPACE phicore::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-transport-api
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/phi-transport-apiConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/phi-transport-apiConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-transport-api
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/phi-transport-apiConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/phi-transport-apiConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/phi-transport-apiConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-transport-api
)
