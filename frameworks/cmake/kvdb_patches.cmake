# SPDX-License-Identifier: Apache-2.0
# Apply maintained fixes in the build tree, never in official checkouts.

function(bk7258_kvdb_patch_copy source_root output_root patch)
  file(MAKE_DIRECTORY "${output_root}")
  foreach(relative IN LISTS ARGN)
    configure_file("${source_root}/${relative}"
                   "${output_root}/${relative}" COPYONLY)
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
  find_package(Git REQUIRED)
  execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check "${patch}"
                  WORKING_DIRECTORY "${output_root}" RESULT_VARIABLE result
                  ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "KVDB patch no longer applies: ${patch}\n${error}")
  endif()
  execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
                  WORKING_DIRECTORY "${output_root}" RESULT_VARIABLE result
                  ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "KVDB patch failed: ${patch}\n${error}")
  endif()
endfunction()

function(bk7258_kvdb_replace_source target original replacement)
  get_filename_component(original "${original}" ABSOLUTE)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "KVDB requires target ${target}")
  endif()
  get_target_property(root ${target} SOURCE_DIR)
  get_target_property(sources ${target} SOURCES)
  set(updated)
  set(count 0)
  foreach(source IN LISTS sources)
    get_filename_component(absolute "${source}" ABSOLUTE BASE_DIR "${root}")
    if(absolute STREQUAL original)
      list(APPEND updated "${replacement}")
      math(EXPR count "${count} + 1")
    else()
      list(APPEND updated "${source}")
    endif()
  endforeach()
  if(NOT count EQUAL 1)
    message(FATAL_ERROR "Expected one ${original} source in ${target}, got ${count}")
  endif()
  set_property(TARGET ${target} PROPERTY SOURCES "${updated}")
endfunction()

function(bk7258_apply_kvdb_patches)
  if(NOT CONFIG_KVDB_PERSIST_PATH MATCHES "^/mnt/sdnand/[^/]+$"
     OR CONFIG_KVDB_PERSIST_PATH MATCHES "/[.][.]?$")
    message(FATAL_ERROR "BKVoice KVDB path must be one file under /mnt/sdnand")
  endif()
  if(NOT CONFIG_KVDB_DIRECT OR NOT CONFIG_KVDB_UNQLITE)
    message(FATAL_ERROR "BKVoice preferences currently require DIRECT journaled UnQLite")
  endif()
  set(framework "${NUTTX_APPS_DIR}/frameworks/system/utils")
  set(engine "${NUTTX_APPS_DIR}/external/unqlite/unqlite")
  get_filename_component(overlay "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  set(external "${NUTTX_DIR}/../vendor/beken/external")
  set(output "${CMAKE_BINARY_DIR}/bk7258-kvdb")
  bk7258_kvdb_patch_copy("${framework}" "${output}/framework"
    "${overlay}/patches/kvdb/0002-unqlite-explicit-journaled-commit.patch"
    kvdb/unqlite.c kvdb/direct.c kvdb/internal.h)
  bk7258_kvdb_patch_copy("${engine}" "${output}/engine"
    "${external}/patches/unqlite/0001-propagate-commit-sync-errors.patch"
    unqlite.c unqlite.h)
  foreach(source unqlite.c direct.c)
    bk7258_kvdb_replace_source(framework_utils "${framework}/kvdb/${source}"
                              "${output}/framework/kvdb/${source}")
  endforeach()
  bk7258_kvdb_replace_source(unqlite "${engine}/unqlite.c"
                            "${output}/engine/unqlite.c")
  if(CONFIG_KVDB_TEMPORARY_STORAGE)
    bk7258_kvdb_patch_copy("${framework}" "${output}/file"
      "${overlay}/patches/kvdb/0001-file-handle-partial-interrupted-io.patch"
      kvdb/file.c kvdb/internal.h)
    bk7258_kvdb_replace_source(framework_utils "${framework}/kvdb/file.c"
                              "${output}/file/kvdb/file.c")
  endif()
endfunction()

# Apps/system is visited before the framework and external libraries exist.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL bk7258_apply_kvdb_patches)
