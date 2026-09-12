# SPDX-License-Identifier: Apache-2.0
# Apply Bluetooth fixes to generated translation units, never to NuttX sources.

function(bk7258_patch_wireless_source source_name patch_name)
  if(NOT TARGET wireless)
    message(FATAL_ERROR "Bluetooth patch requires the wireless target")
  endif()
  set(source "${NUTTX_DIR}/wireless/bluetooth/${source_name}")
  set(output_root "${CMAKE_BINARY_DIR}/bk7258-bluetooth")
  set(output "${output_root}/wireless/bluetooth/${source_name}")
  set(staging_root "${CMAKE_BINARY_DIR}/bk7258-bluetooth-stage")
  set(staging "${staging_root}/wireless/bluetooth/${source_name}")
  set(patch "${NUTTX_DIR}/../vendor/beken/nuttx/patches/bluetooth/${patch_name}")
  get_filename_component(staging_dir "${staging}" DIRECTORY)
  get_filename_component(output_dir "${output}" DIRECTORY)
  file(MAKE_DIRECTORY "${staging_dir}" "${output_dir}")
  configure_file("${source}" "${staging}" COPYONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${source}" "${patch}")
  find_package(Git REQUIRED)
  execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check "${patch}"
                  WORKING_DIRECTORY "${staging_root}" RESULT_VARIABLE result
                  ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Bluetooth patch no longer applies: ${patch}\n${error}")
  endif()
  execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
                  WORKING_DIRECTORY "${staging_root}" RESULT_VARIABLE result
                  ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Bluetooth patch failed: ${patch}\n${error}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                  "${staging}" "${output}" RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Bluetooth generated source copy failed: ${output}")
  endif()
  get_target_property(root wireless SOURCE_DIR)
  get_target_property(sources wireless SOURCES)
  set(updated)
  set(count 0)
  foreach(candidate IN LISTS sources)
    get_filename_component(absolute "${candidate}" ABSOLUTE BASE_DIR "${root}")
    if(absolute STREQUAL source)
      list(APPEND updated "${output}")
      math(EXPR count "${count} + 1")
    else()
      list(APPEND updated "${candidate}")
    endif()
  endforeach()
  if(NOT count EQUAL 1)
    message(FATAL_ERROR "Expected one ${source} source in wireless, got ${count}")
  endif()
  set_property(TARGET wireless PROPERTY SOURCES "${updated}")
endfunction()

function(bk7258_apply_gatt_ccc_patch)
  target_include_directories(wireless PRIVATE
                             "${NUTTX_DIR}/wireless/bluetooth")
  bk7258_patch_wireless_source(
    "bt_gatt.c" "0003-gatt-ccc-do-not-allocate-unbonded-key-slot.patch")
  bk7258_patch_wireless_source(
    "bt_att.c" "0004-att-cap-mtu-to-receive-buffer.patch")
endfunction()

# apps/system is configured before all NuttX component targets exist.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL bk7258_apply_gatt_ccc_patch)
