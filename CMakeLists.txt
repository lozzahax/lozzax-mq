cmake_minimum_required(VERSION 3.7)

project(liblozzaxmq CXX)

include(GNUInstallDirs)

set(LOZZMQ_VERSION_MAJOR 1)
set(LOZZMQ_VERSION_MINOR 1)
set(LOZZMQ_VERSION_PATCH 3)
set(LOZZMQ_VERSION "${LOZZMQ_VERSION_MAJOR}.${LOZZMQ_VERSION_MINOR}.${LOZZMQ_VERSION_PATCH}")
message(STATUS "lozzaxmq v${LOZZMQ_VERSION}")

set(LOZZMQ_LIBVERSION 0)


option(BUILD_SHARED_LIBS "Build shared libraries instead of static ones" ON)


list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

configure_file(lozzaxmq/version.h.in lozzaxmq/version.h @ONLY)
configure_file(liblozzaxmq.pc.in liblozzaxmq.pc @ONLY)

add_library(lozzaxmq
    lozzaxmq/auth.cpp
    lozzaxmq/bt_serialize.cpp
    lozzaxmq/connections.cpp
    lozzaxmq/jobs.cpp
    lozzaxmq/lozzaxmq.cpp
    lozzaxmq/proxy.cpp
    lozzaxmq/worker.cpp
)
set_target_properties(lozzaxmq PROPERTIES SOVERSION ${LOZZMQ_LIBVERSION})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(lozzaxmq PRIVATE Threads::Threads)

# libzmq is nearly impossible to link statically from a system-installed static library: it depends
# on a ton of other libraries, some of which are not all statically available.  If the caller wants
# to mess with this, so be it: they can set up a libzmq target and we'll use it.  Otherwise if they
# asked us to do things statically, don't even try to find a system lib and just build it.
set(lozzaxmq_build_static_libzmq OFF)
if(TARGET libzmq)
    target_link_libraries(lozzaxmq PUBLIC libzmq)
elseif(BUILD_SHARED_LIBS)
    include(FindPkgConfig)
    pkg_check_modules(libzmq libzmq>=4.3 IMPORTED_TARGET)
    if(libzmq_FOUND)
        target_link_libraries(lozzaxmq PUBLIC PkgConfig::libzmq)
    else()
        set(lozzaxmq_build_static_libzmq ON)
    endif()
else()
    set(lozzaxmq_build_static_libzmq ON)
endif()

if(lozzaxmq_build_static_libzmq)
    message(STATUS "libzmq >= 4.3 not found or static build requested, building bundled 4.3.2")
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/local-libzmq")
    include(LocalLibzmq)
    target_link_libraries(lozzaxmq PUBLIC libzmq_vendor)
endif()

target_include_directories(lozzaxmq
    PUBLIC
        $<INSTALL_INTERFACE:>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cppzmq>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/mapbox-variant/include>
)

target_compile_options(lozzaxmq PRIVATE -Wall -Wextra -Werror)
set_target_properties(lozzaxmq PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

function(link_dep_libs target linktype libdirs)
    foreach(lib ${ARGN})
        find_library(link_lib-${lib} NAMES ${lib} PATHS ${libdirs})
        message(STATUS "FIND ${lib} FOUND ${link_lib-${lib}}")
        if(link_lib-${lib})
            target_link_libraries(${target} ${linktype} ${link_lib-${lib}})
        endif()
    endforeach()
endfunction()

# If the caller has already set up a sodium target then we will just link to it, otherwise we go
# looking for it.
if(TARGET sodium)
    target_link_libraries(lozzaxmq PRIVATE sodium)
    if(lozzaxmq_build_static_libzmq)
        target_link_libraries(libzmq_vendor INTERFACE sodium)
    endif()
else()
    pkg_check_modules(sodium REQUIRED libsodium IMPORTED_TARGET)

    if(BUILD_SHARED_LIBS)
        target_link_libraries(lozzaxmq PRIVATE PkgConfig::sodium)
        if(lozzaxmq_build_static_libzmq)
            target_link_libraries(libzmq_vendor INTERFACE PkgConfig::sodium)
        endif()
    else()
        link_dep_libs(lozzaxmq PRIVATE "${sodium_STATIC_LIBRARY_DIRS}" ${sodium_STATIC_LIBRARIES})
        target_include_directories(lozzaxmq PRIVATE ${sodium_STATIC_INCLUDE_DIRS})
        if(lozzaxmq_build_static_libzmq)
            link_dep_libs(libzmq_vendor INTERFACE "${sodium_STATIC_LIBRARY_DIRS}" ${sodium_STATIC_LIBRARIES})
            target_link_libraries(libzmq_vendor INTERFACE ${sodium_STATIC_INCLUDE_DIRS})
        endif()
    endif()
endif()

add_library(lozzaxmq::lozzaxmq ALIAS lozzaxmq)

export(
    TARGETS lozzaxmq
    NAMESPACE lozzaxmq::
    FILE lozzaxmqTargets.cmake
)
install(
    TARGETS lozzaxmq
    EXPORT lozzaxmqConfig
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
    FILES lozzaxmq/auth.h
          lozzaxmq/batch.h
          lozzaxmq/bt_serialize.h
          lozzaxmq/connections.h
          lozzaxmq/hex.h
          lozzaxmq/lozzaxmq.h
          lozzaxmq/message.h
          lozzaxmq/string_view.h
          ${CMAKE_CURRENT_BINARY_DIR}/lozzaxmq/version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lozzaxmq
)
option(LOZZMQ_INSTALL_MAPBOX_VARIANT "Install mapbox-variant headers with lozzaxmq/ headers" ON)
if(LOZZMQ_INSTALL_MAPBOX_VARIANT)
  install(
    FILES mapbox-variant/include/mapbox/variant.hpp
          mapbox-variant/include/mapbox/variant_cast.hpp
          mapbox-variant/include/mapbox/variant_io.hpp
          mapbox-variant/include/mapbox/variant_visitor.hpp
          mapbox-variant/include/mapbox/recursive_wrapper.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lozzaxmq/mapbox
  )
endif()

option(LOZZMQ_INSTALL_CPPZMQ "Install cppzmq header with lozzaxmq/ headers" ON)
if(LOZZMQ_INSTALL_CPPZMQ)
  install(
    FILES cppzmq/zmq.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lozzaxmq
  )
endif()


install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/liblozzaxmq.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(lozzaxmq_IS_TOPLEVEL_PROJECT TRUE)
else()
  set(lozzaxmq_IS_TOPLEVEL_PROJECT FALSE)
endif()

option(LOZZMQ_BUILD_TESTS "Building and perform lozzaxmq tests" ${lozzaxmq_IS_TOPLEVEL_PROJECT})
if(LOZZMQ_BUILD_TESTS)
    add_subdirectory(tests)
endif()

