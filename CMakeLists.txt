#
# Copyright (C) 2023 Xiaomi Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#

if(CONFIG_OPTEE_OS)
  add_subdirectory(wasm)

  nuttx_add_prebuilt(BIN_TARGETS optee_nuttx APP_TARGETS
                     ${CONFIG_OPTEE_SERVER_PROGNAME})

  # ############################################################################
  # Flags
  # ############################################################################

  set(CFLAGS
      -DCFG_CORE_DYN_SHM -DCFG_NUM_THREADS=1 -DCFG_OTP_SUPPORT
      -DCFG_OTP_SUPPORT_NO_PROVISION_TMP -DCFG_WITH_USER_TA
      -DHOST_FS_PARENT_PATH=\"${CONFIG_OPTEE_HOST_FS_PARENT_PATH}\")

  if(CONFIG_TEE_CORE_DEBUG)
    list(APPEND CFLAGS -DCFG_TEE_CORE_DEBUG)
  endif()

  list(APPEND CFLAGS -D__text_start=_stext)

  if(CONFIG_DEBUG_INFO)
    list(APPEND CFLAGS -DTRACE_LEVEL=3)
  elseif(CONFIG_DEBUG_WARN)
    list(APPEND CFLAGS -DTRACE_LEVEL=2)
  elseif(CONFIG_DEBUG_ERROR)
    list(APPEND CFLAGS -DTRACE_LEVEL=1)
  else()
    list(APPEND CFLAGS -DTRACE_LEVEL=1)
  endif()

  if(CONFIG_ARCH_64BIT)
    list(APPEND CFLAGS -DARM64)
  else()
    list(APPEND CFLAGS -DARM32)
  endif()

  # ############################################################################
  # Sources
  # ############################################################################

  set(CSRCS
      compat/abort.c
      compat/atomic.c
      compat/core_mmu.c
      compat/hmac_memory.c
      compat/host_fs.c
      compat/ldelf_loader.c
      compat/malloc.c
      compat/mempool.c
      compat/mobj.c
      compat/mobj_dyn_shm.c
      compat/msg_param.c
      compat/notif.c
      compat/otp_stubs.c
      compat/panic.c
      compat/random.c
      compat/scall.c
      compat/spinlock.c
      compat/tee_time_system.c
      compat/thread.c
      compat/thread_optee_compat.c
      compat/trace_ext.c
      compat/ts_manager.c
      compat/user_access.c
      compat/user_mode_ctx.c
      compat/user_ta_entry_dummy.c
      compat/vm.c)

  if(CONFIG_TEE_CORE_DEBUG)
    list(APPEND CSRCS compat/spin_lock_debug.c)
  endif()

  if(CONFIG_OPTEE_RPMB_FS)
    list(APPEND CSRCS compat/rpmb_fs.c)
  endif()

  if(CONFIG_OPTEE_COMPAT_MITEE_FS)
    list(
      APPEND
      CSRCS
      compat/mitee_compat/huk_subkey.c
      compat/mitee_compat/mitee_crypt.c
      compat/mitee_compat/tee_fs_rpc.c
      compat/mitee_compat/tee_fs_key_manager_compat.c
      compat/mitee_compat/tee_obj.c
      compat/mitee_compat/tee_ree_fs.c
      compat/mitee_compat/tee_svc_compat.c
      compat/mitee_compat/tee_svc_storage.c)

    list(APPEND CFLAGS -DFS_STORAGE_DIR_PRIVATE=\"/sst/\")
  endif()

  if(CONFIG_USER_TA_WASM)
    list(APPEND CSRCS wasm/user_ta_wasm.c wasm/libtee_builtin_wrapper.c)
    list(APPEND CFLAGS -DUSER_TA_WASM)
  endif()

  if(CONFIG_USER_TA_ELF)
    list(
      APPEND
      CSRCS
      elf/user_ta_elf.c)
  endif()

  # ############################################################################
  # Include Directory
  # ############################################################################

  set(INCDIR ${CMAKE_CURRENT_LIST_DIR}/include)
  if(CONFIG_OPTEE_COMPAT_MITEE_FS)
    list(APPEND INCDIR ${CMAKE_CURRENT_LIST_DIR}/include/mitee_compat)
  endif()

  list(
    APPEND
    INCDIR
    ${NUTTX_APPS_DIR}/external/optee/optee_os/optee_os/core/include
    ${NUTTX_APPS_DIR}/external/optee/optee_os/optee_os/ldelf/include
    ${NUTTX_APPS_DIR}/external/optee/optee_os/optee_os/lib/libutils/ext/include
    ${NUTTX_APPS_DIR}/external/optee/optee_os/optee_os/lib/libutee/include
    ${NUTTX_APPS_DIR}/crypto/mbedtls/mbedtls/library)

  # ############################################################################
  # Library Configuration
  # ############################################################################

  nuttx_library_ifndef(SKIP_PREBUILT optee_nuttx)
  nuttx_sources(${CSRCS})
  nuttx_include_directories(${INCDIR})
  nuttx_compile_options(${CFLAGS})

  # ############################################################################
  # ELF TA Symtab
  # ############################################################################

  if(CONFIG_USER_TA_ELF)
    set(SYMTAB_APPS_SOURCE ${CMAKE_BINARY_DIR}/ta_elf_symtab_apps.c)
    add_custom_command(
      OUTPUT ${SYMTAB_APPS_SOURCE} always_rebuild_symtab
      COMMAND ${NUTTX_APPS_DIR}/tools/mksymtab.sh ${CMAKE_BINARY_DIR}/bin g_elf_ta >
              ${CMAKE_BINARY_DIR}/ta_elf_symtab_apps.c
      COMMAND ${CMAKE_COMMAND} -E touch always_rebuild_symtab
      COMMAND ${CMAKE_COMMAND} -E remove always_rebuild_symtab
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS nuttx_apps_mksymtab
      VERBATIM
      COMMENT "Mksymtab: generating symbol table for elf ta")
    add_custom_target(ta_elf_symtab_source_gen ALL DEPENDS ${SYMTAB_APPS_SOURCE})

    add_library(ta_elf_symtab)
    add_dependencies(ta_elf_symtab ta_elf_symtab_source_gen)
    target_compile_options(ta_elf_symtab PRIVATE ${NO_LTO} -fno-builtin)
    target_sources(ta_elf_symtab PRIVATE ${SYMTAB_APPS_SOURCE})
    nuttx_add_library_internal(ta_elf_symtab)
    set_property(GLOBAL APPEND PROPERTY NUTTX_SYSTEM_LIBRARIES ta_elf_symtab)
  endif()

  # ############################################################################
  # Applications Configuration
  # ############################################################################

  if(NOT CONFIG_OPTEE_SERVER_NONE)
    nuttx_add_application(
      NAME
      ${CONFIG_OPTEE_SERVER_PROGNAME}
      STACKSIZE
      ${CONFIG_OPTEE_SERVER_STACKSIZE}
      PRIORITY
      ${CONFIG_OPTEE_SERVER_PRIORITY}
      SRCS
      server/opteed.c
      INCLUDE_DIRECTORIES
      ${INCDIR}
      COMPILE_FLAGS
      ${CFLAGS}
      DEPENDS
      optee_nuttx)
  endif()

endif()
