cmake_minimum_required(VERSION 2.8)
project(fluent-bit)

# Update CFLAGS
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fPIC -Wno-deprecated-declarations ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__FILENAME__='\"$(subst ${CMAKE_SOURCE_DIR}/,,$(abspath $<))\"'")

include(GNUInstallDirs)
include(ExternalProject)

# Output paths
set(FLB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/library")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/library")

# Fluent Bit Version
set(FLB_VERSION_MAJOR  0)
set(FLB_VERSION_MINOR 10)
set(FLB_VERSION_PATCH  0)
set(FLB_VERSION_STR "${FLB_VERSION_MAJOR}.${FLB_VERSION_MINOR}.${FLB_VERSION_PATCH}")

# Build Options
option(FLB_ALL                "Enable all features"          No)
option(FLB_DEBUG              "Build with debug symbols"     No)
option(FLB_JEMALLOC           "Build with Jemalloc support"  No)
option(FLB_TLS                "Build with SSL/TLS support"   No)
option(FLB_HTTP               "Enable build-in HTTP Server"  No)
option(FLB_WITHOUT_BIN        "Do not build executable"      No)
option(FLB_WITHOUT_EXAMPLES   "Do not build examples"        No)
option(FLB_WITHOUT_SHARED_LIB "Do not build shared library"  No)
option(FLB_STATS              "Enable stats support"         No)
option(FLB_VALGRIND           "Enable Valgrind support"      No)
option(FLB_TRACE              "Enable trace mode"            No)
option(FLB_TESTS              "Enable tests"                 No)
option(FLB_MTRACE             "Enable mtrace support"        No)
option(FLB_BUFFERING          "Enable buffering support"     No)
option(FLB_POSIX_TLS          "Force POSIX thread storage"   No)

# Proxy Plugins
option(FLB_PROXY_GO           "Enable Go plugins support"    No)

# Advanced options for Flushing methods
# =====================================
# Flush methods defines under what kind of context/environment the plugins
# flush callback is invoked. Since this is done through co-routines
# there are two options available:
#
# FLB_FLUSH_LIBCO: set by default, use lib/flb_libco implementation which
# supports amd64, arm, x86 and ppc.
#
# FLB_FLUSH_UCONTEXT: It uses POSIX ucontext for co-routines implementation.
# Despites this is POSIX deprecated, it's supported on Linux and BSD
# systems, but specific old-toolchains do not implement it.

option(FLB_FLUSH_UCONTEXT     "Use co-routines for flush I/O"  No)
option(FLB_FLUSH_LIBCO        "Use libco for flush I/O"       Yes)

# Build Plugins
option(FLB_IN_XBEE     "Enable XBee input plugin"            No)
option(FLB_IN_CPU      "Enable CPU input plugin"            Yes)
option(FLB_IN_FORWARD  "Enable Forward input plugin"        Yes)
option(FLB_IN_HEALTH   "Enable Health input plugin"         Yes)
option(FLB_IN_HTTP     "Enable HTTP input plugin"            No)
option(FLB_IN_MEM      "Enable Memory input plugin"         Yes)
option(FLB_IN_KMSG     "Enable Kernel log input plugin"     Yes)
option(FLB_IN_LIB      "Enable library mode input plugin"   Yes)
option(FLB_IN_RANDOM   "Enable random input plugin"         Yes)
option(FLB_IN_SERIAL   "Enable Serial input plugin"         Yes)
option(FLB_IN_STDIN    "Enable Standard input plugin"       Yes)
option(FLB_IN_TCP      "Enable TCP input plugin"            Yes)
option(FLB_IN_MQTT     "Enable MQTT Broker input plugin"    Yes)
option(FLB_IN_HEAD     "Enable Head input plugin"           Yes)
option(FLB_OUT_ES      "Enable Elasticsearch output plugin" Yes)
option(FLB_OUT_FORWARD "Enable Forward output plugin"       Yes)
option(FLB_OUT_HTTP    "Enable HTTP output plugin"          Yes)
option(FLB_OUT_NATS    "Enable NATS output plugin"           No)
option(FLB_OUT_PLOT    "Enable Plot output plugin"          Yes)
option(FLB_OUT_TD      "Enable Treasure Data output plugin" Yes)
option(FLB_OUT_RETRY   "Enable Retry test output plugin"     No)
option(FLB_OUT_STDOUT  "Enable STDOUT output plugin"        Yes)
option(FLB_OUT_LIB     "Enable library mode output plugin"  Yes)
option(FLB_OUT_NULL    "Enable dev null output plugin"      Yes)
option(FLB_OUT_FLOWCOUNT "Enable flowcount output plugin"   Yes)

# Enable all features
if(FLB_ALL)
  # Global
  set(FLB_DEBUG        1)
  set(FLB_TLS          1)
  set(FLB_BUFFERING    1)

  # Input plugins
  set(FLB_IN_XBEE      1)
  set(FLB_IN_CPU       1)
  set(FLB_IN_MEM       1)
  set(FLB_IN_KMSG      1)
  set(FLB_IN_MQTT      1)
  set(FLB_IN_SERIAL    1)
  set(FLB_IN_STDIN     1)
  set(FLB_IN_HEAD      1)

  # Output plugins
  set(FLB_OUT_ES       1)
  set(FLB_OUT_FORWARD  1)
  set(FLB_OUT_HTTP     1)
  set(FLB_OUT_NATS     1)
  set(FLB_OUT_NULL     1)
  set(FLB_OUT_PLOT     1)
  set(FLB_OUT_RETRY    1)
  set(FLB_OUT_TD       1)
  set(FLB_OUT_STDOUT   1)
  set(FLB_OUT_LIB      1)
  set(FLB_OUT_FLOWCOUNT 1)
endif()

# Macro to set definitions
macro(FLB_DEFINITION var)
  add_definitions(-D${var})
  set(FLB_BUILD_FLAGS "${FLB_BUILD_FLAGS}#ifndef ${var}\n#define ${var}\n#endif\n")
  set(FLB_INFO_FLAGS "${FLB_INFO_FLAGS} ${var}")
endmacro()

macro(FLB_OPTION option value)
  set(${option} ${value} CACHE "" INTERNAL FORCE)
endmacro()

# Enable Debug symbols if specified
if(FLB_DEBUG)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

# General Headers
include_directories(
  include/
  lib/
  lib/ejson
  lib/flb_libco
  lib/sha1
  lib/msgpack-c-068041f/include
  lib/monkey/include
  lib/monkey/mk_core/include
  )

# mk_core is aware about jemalloc usage, we need to disable this as
# fluent-bit do not use it.
set(WITH_SYSTEM_MALLOC  1 CACHE BOOL "Use system memory allocator")

# MsgPack options
option(MSGPACK_ENABLE_CXX             OFF)
option(MSGPACK_ENABLE_SHARED          OFF)
option(MSGPACK_BUILD_TESTS            OFF)
option(MSGPACK_BUILD_EXAMPLES         OFF)
add_subdirectory(lib/msgpack-c-068041f)

# Lib: build the core libraries used by Fluent-Bit
FLB_DEFINITION(JSMN_PARENT_LINKS)
FLB_DEFINITION(JSMN_STRICT)
add_subdirectory(lib/jsmn)
add_subdirectory(lib/ejson)

if(FLB_BUFFERING)
  add_subdirectory(lib/sha1)
endif()

# MK Core
macro(MK_SET_OPTION option value)
  set(${option} ${value} CACHE "" INTERNAL FORCE)
endmacro()
MK_SET_OPTION(MK_SYSTEM_MALLOC      ON)

if(FLB_HTTP)
  MK_SET_OPTION(MK_WITHOUT_BIN        ON)
  MK_SET_OPTION(MK_WITHOUT_CONF       ON)
  MK_SET_OPTION(MK_PLUGIN_AUTH       OFF)
  MK_SET_OPTION(MK_PLUGIN_CGI        OFF)
  MK_SET_OPTION(MK_PLUGIN_CHEETAH    OFF)
  MK_SET_OPTION(MK_PLUGIN_DIRLISTING OFF)
  MK_SET_OPTION(MK_PLUGIN_FASTCGI    OFF)
  MK_SET_OPTION(MK_PLUGIN_LOGGER     OFF)
  MK_SET_OPTION(MK_PLUGIN_MANDRIL    OFF)
  MK_SET_OPTION(MK_PLUGIN_TLS        OFF)
  add_subdirectory(lib/monkey)
else()
  add_subdirectory(lib/monkey/mk_core)
endif()

# SSL/TLS: add encryption support
if(FLB_OUT_TD)
  set(FLB_TLS ON)
endif()

if(FLB_TLS)
  FLB_DEFINITION(FLB_HAVE_TLS)
  option(ENABLE_TESTING  OFF)
  option(ENABLE_PROGRAMS OFF)
  option(INSTALL_MBEDTLS_HEADERS OFF)
  add_subdirectory(lib/mbedtls-yotta-2.3.0)
  include_directories(lib/mbedtls-yotta-2.3.0/include)
endif()

if(FLB_BUFFERING)
  FLB_DEFINITION(FLB_HAVE_BUFFERING)
endif()

# Enable cJSON
add_subdirectory(lib/cjson)
include_directories(lib/cjson)

if(FLB_TRACE)
  FLB_DEFINITION(FLB_HAVE_TRACE)
endif()

if(FLB_STATS)
  FLB_DEFINITION(FLB_HAVE_STATS)
endif()

if(FLB_HTTP)
  FLB_DEFINITION(FLB_HAVE_HTTP)
endif()

# Flush method
check_c_source_compiles("
  #define _XOPEN_SOURCE
  #define _BSD_SOURCE
  #include <ucontext.h>
  int main() {
     ucontext_t a, b;
     return swapcontext(&a, &b);
  }" FLB_HAVE_UCONTEXT
  FAIL_REGEX "warning.*not implemented.*")

if(FLB_FLUSH_UCONTEXT AND FLB_HAVE_UCONTEXT)
  FLB_DEFINITION(FLB_HAVE_FLUSH_UCONTEXT)
  FLB_OPTION(FLB_FLUSH_LIBCO OFF)
else()
  FLB_DEFINITION(FLB_HAVE_FLUSH_LIBCO)
  FLB_OPTION(FLB_FLUSH_LIBCO ON)
  add_subdirectory(lib/flb_libco)
endif()

# Valgrind support
check_c_source_compiles("
  #include <valgrind/valgrind.h>
  int main() {
     return 0;
  }" FLB_HAVE_VALGRIND)

if(FLB_VALGRIND OR FLB_HAVE_VALGRIND)
  FLB_DEFINITION(FLB_HAVE_VALGRIND)
endif()

# mtrace support
if(FLB_MTRACE)
  check_c_source_compiles("
    #include <mcheck.h>
    int main() {
       return 0;
    }" FLB_HAVE_MTRACE)

  if(FLB_HAVE_MTRACE AND FLB_DEBUG)
    FLB_DEFINITION(FLB_HAVE_MTRACE)
  endif()
endif()

if(FLB_TD)
  FLB_DEFINITION(FLB_IS_TD_AGENT)
  FLB_OPTION(FLB_JEMALLOC ON)
  set(FLB_PROG_NAME "TD Agent Bit")
  set(FLB_OUT_NAME "td-agent-bit")
  set(WITHOUT_HEADERS On)
else()
  set(FLB_PROG_NAME "Fluentd Bit")
  set(FLB_OUT_NAME "fluent-bit")
endif()

if(FLB_PROXY_GO)
  FLB_DEFINITION(FLB_HAVE_PROXY_GO)
endif()

# Memory Allocator
# ================
if(FLB_JEMALLOC AND ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  FLB_DEFINITION(FLB_HAVE_JEMALLOC)
  FLB_DEFINITION(JEMALLOC_MANGLE)

  # Link to Jemalloc as an external dependency
  ExternalProject_Add(jemalloc
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/jemalloc
    CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/lib/jemalloc/configure --with-lg-quantum=3 --enable-cc-silence --prefix=<INSTALL_DIR>
    CFLAGS=-std=gnu99\ -Wall\ -pipe\ -g3\ -O3\ -funroll-loops
    BUILD_COMMAND ${MAKE}
    INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/
    INSTALL_COMMAND $(MAKE) install_lib_static install_include)
  add_library(libjemalloc STATIC IMPORTED GLOBAL)
  set_target_properties(libjemalloc PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/lib/libjemalloc_pic.a")
  add_dependencies(libjemalloc jemalloc)
  include_directories("${CMAKE_CURRENT_BINARY_DIR}/include/")
else()
  FLB_OPTION(FLB_JEMALLOC OFF)
endif()

# Pthread Local Storage
# =====================
# By default we expect the compiler already support thread local storage
# through __thread type, otherwise Fluent Bit fallback to the old POSIX
# pthread mode (pthread_key_t), or it can be forced setting FLB_POSIX_TLS
# for testing/compatibility purposes.
if(NOT FLB_POSIX_TLS)
  check_c_source_compiles("
   __thread int a;
   int main() {
       __tls_get_addr(0);
       return 0;
   }" FLB_HAVE_C_TLS)
  if(FLB_HAVE_C_TLS)
    FLB_DEFINITION(FLB_HAVE_C_TLS)
  endif()
endif()

# setjmp
check_c_source_compiles("
    #include <setjmp.h>
    int main() {
        return 0;
    }" FLB_HAVE_SETJMP)
if(FLB_HAVE_SETJMP)
  FLB_DEFINITION(FLB_HAVE_SETJMP)
endif()

# accept(4)
check_c_source_compiles("
    #define _GNU_SOURCE
    #include <stdio.h>
    #include <sys/socket.h>
    int main() {
        accept4(0, NULL, NULL, 0);
        return 0;
    }" FLB_HAVE_ACCEPT4)
if(FLB_HAVE_ACCEPT4)
  FLB_DEFINITION(FLB_HAVE_ACCEPT4)
endif()

configure_file(
  "${PROJECT_SOURCE_DIR}/include/fluent-bit/flb_info.h.in"
  "${PROJECT_SOURCE_DIR}/include/fluent-bit/flb_info.h"
  )

configure_file(
  "${PROJECT_SOURCE_DIR}/include/fluent-bit/flb_version.h.in"
  "${PROJECT_SOURCE_DIR}/include/fluent-bit/flb_version.h"
  )

# Instruct CMake to build the Fluent Bit Core
add_subdirectory(include)
add_subdirectory(plugins)
add_subdirectory(src)

if(NOT FLB_WITHOUT_EXAMPLES)
  add_subdirectory(examples)
endif()

if(FLB_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()


### CPACK / RPM
set(CPACK_PACKAGE_VERSION ${FLB_VERSION_STR})
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME "td-agent-bit")
set(CPACK_PACKAGE_RELEASE 1)
set(CPACK_PACKAGE_CONTACT "Eduardo Silva <eduardo@treasure-data.com>")
set(CPACK_PACKAGE_VENDOR "Treasure Data")
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/td-agent-bit")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RPM_PACKAGE_GROUP "System Environment/Daemons")
set(CPACK_RPM_PACKAGE_LICENSE "Apache v2.0")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${PROJECT_SOURCE_DIR}/cpack/description")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast data collector for Linux")
set(CPACK_RPM_SPEC_MORE_DEFINE "%define ignore \#")
set(CPACK_RPM_USER_FILELIST
	"%ignore /lib"
	"%ignore /lib/systemd"
	"%ignore /lib/systemd/system"
	"%ignore /lib64"
	"%ignore /lib64/pkgconfig"
	"%ignore /opt"
	"%ignore /etc")
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
include(CPack)
