cmake_minimum_required(VERSION 3.20)
# ASM compiles the ShadowHook arm64 trampoline glue (arch/arm64/sh_glue.S).
project(febrian C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- ABI guard ---------------------------------------------------------------
if(NOT DEFINED ANDROID_ABI OR NOT ANDROID_ABI MATCHES "^arm64-v8a$")
  message(FATAL_ERROR
    "Unsupported or missing ANDROID_ABI='${ANDROID_ABI}'. "
    "Use the NDK toolchain file with arm64-v8a.")
endif()

# --- ShadowHook (sources fetched by CI, linked statically) -------------------
# Upstream's own CMakeLists builds a shared library and a companion stub, which
# a one-file delivery cannot ship, so only the sources are reused here. sh_jni.c
# is excluded because it defines a second JNI_OnLoad and that symbol must stay
# ours.
set(SHADOWHOOK_ROOT "" CACHE PATH "Root of the fetched ShadowHook source tree")
if(NOT SHADOWHOOK_ROOT)
  message(FATAL_ERROR "Pass -DSHADOWHOOK_ROOT=<dir of the fetched ShadowHook source tree>")
endif()

set(SHADOWHOOK_SRC "${SHADOWHOOK_ROOT}/shadowhook/src/main/cpp")
if(NOT EXISTS "${SHADOWHOOK_SRC}/include/shadowhook.h")
  message(FATAL_ERROR "shadowhook.h not found in ${SHADOWHOOK_SRC}/include")
endif()

file(GLOB SHADOWHOOK_CORE "${SHADOWHOOK_SRC}/*.c")
list(FILTER SHADOWHOOK_CORE EXCLUDE REGEX "/sh_jni\\.c$")
file(GLOB SHADOWHOOK_ARCH "${SHADOWHOOK_SRC}/arch/arm64/*.c")
file(GLOB SHADOWHOOK_ARCH_ASM "${SHADOWHOOK_SRC}/arch/arm64/*.S")
file(GLOB SHADOWHOOK_COMMON "${SHADOWHOOK_SRC}/common/*.c")
file(GLOB SHADOWHOOK_XDL "${SHADOWHOOK_SRC}/third_party/xdl/*.c")

add_library(shadowhook STATIC
  ${SHADOWHOOK_CORE}
  ${SHADOWHOOK_ARCH}
  ${SHADOWHOOK_ARCH_ASM}
  ${SHADOWHOOK_COMMON}
  ${SHADOWHOOK_XDL}
)

target_compile_features(shadowhook PRIVATE c_std_11)
target_include_directories(shadowhook PRIVATE
  "${SHADOWHOOK_SRC}"
  "${SHADOWHOOK_SRC}/arch/arm64"
  "${SHADOWHOOK_SRC}/common"
  "${SHADOWHOOK_SRC}/third_party/xdl"
  "${SHADOWHOOK_SRC}/third_party/bsd"
  "${SHADOWHOOK_SRC}/third_party/lss"
)
target_include_directories(shadowhook PUBLIC "${SHADOWHOOK_SRC}/include")

# Hidden visibility keeps shadowhook_* and sh_* out of .dynsym; the archive is
# stripped from the export table too by --exclude-libs,ALL on the final link.
target_compile_options(shadowhook PRIVATE
  -Oz
  -fvisibility=hidden
  -ffunction-sections
  -fdata-sections
  -fno-unwind-tables
  -fno-asynchronous-unwind-tables
  -fno-stack-protector
  -fno-ident
  -fomit-frame-pointer
)
set_target_properties(shadowhook PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Project -----------------------------------------------------------------
# File logging is on by default; -DBOKU_LOG=OFF drops it for the smallest .so.
option(BOKU_LOG "Enable file logging" ON)

add_library(febrian SHARED
  # entry point and platform bridge
  src/main.cpp
  src/jni_bridge.cpp
  # shared services
  src/log.cpp
  src/toast.cpp
  src/hook.cpp
  src/il2cpp_api.cpp
  src/targets.cpp
  # hook groups
  src/hooks/iap.cpp
  src/hooks/multiplayer.cpp
  src/hooks/text_input.cpp
  src/hooks/profile.cpp
  src/hooks/password_room.cpp
  src/hooks/skin_range.cpp
  src/hooks/about.cpp
  src/hooks/food.cpp
)

target_include_directories(febrian PRIVATE src)

# Size and codegen: no exceptions, RTTI or unwind tables, and one section per
# function and data item so the linker can drop what no hook reaches.
target_compile_options(febrian PRIVATE
  -Oz
  -flto
  -fno-exceptions
  -fno-rtti
  -fno-unwind-tables
  -fno-asynchronous-unwind-tables
  -fno-stack-protector
  -fno-ident
  -fomit-frame-pointer
  -ffunction-sections
  -fdata-sections
)

target_link_options(febrian PRIVATE
  -flto
  -Wl,--gc-sections
  -Wl,--icf=all
  -Wl,--strip-all
  -Wl,--exclude-libs,ALL
  -Wl,--build-id=none
  -Wl,--hash-style=gnu
  -Wl,--no-rosegment
  -Wl,-z,norelro
  -Wl,-z,max-page-size=16384
  -Wl,--pack-dyn-relocs=android
)

# shadowhook: static archive. dl: dlopen, dlsym, dladdr. log: ShadowHook's own
# __android_log_print calls, which stay linked even with debuggable false.
target_link_libraries(febrian PRIVATE shadowhook dl log)

if(BOKU_LOG)
  target_compile_definitions(febrian PRIVATE BOKU_LOG)
endif()

# Hidden by default; JNI_OnLoad opts back in with an explicit visibility
# attribute. Output: <build>/lib/<ANDROID_ABI>/libfebrian.so
set_target_properties(febrian PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  OUTPUT_NAME "febrian"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}"
)
