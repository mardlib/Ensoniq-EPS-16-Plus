foreach(REQUIRED ARM_BUNDLE X86_BUNDLE PACKAGE_DIR RESOURCE_README INSTALLER)
  if(NOT DEFINED ${REQUIRED})
    message(FATAL_ERROR "${REQUIRED} is required")
  endif()
endforeach()
if(NOT EXISTS "${ARM_BUNDLE}" OR NOT EXISTS "${X86_BUNDLE}")
  message(FATAL_ERROR "Both arm64 and x86_64 VST3 bundles must exist")
endif()

set(PLUGIN_NAME "Ensoniq EPS-16 Plus")
set(STAGE "${PACKAGE_DIR}/${PLUGIN_NAME}.stage")
set(FINAL "${PACKAGE_DIR}/${PLUGIN_NAME}.vst3")
set(RESOURCES "${PACKAGE_DIR}/EPS_files")
set(ARCHIVE "${PACKAGE_DIR}/Ensoniq-EPS-16-Plus-macOS-universal.zip")
set(ARM_BINARY "${ARM_BUNDLE}/Contents/MacOS/${PLUGIN_NAME}")
set(X86_BINARY "${X86_BUNDLE}/Contents/MacOS/${PLUGIN_NAME}")
set(STAGE_BINARY "${STAGE}/Contents/MacOS/${PLUGIN_NAME}")
set(INSTALL_COMMAND "${PACKAGE_DIR}/Install EPS-16 Plus.command")
set(PACKAGE_ITEMS "${PLUGIN_NAME}.vst3" "EPS_files"
                  "Install EPS-16 Plus.command")

function(run_checked)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE RESULT)
  if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Command failed (${RESULT}): ${ARGV}")
  endif()
endfunction()

file(REMOVE_RECURSE "${PACKAGE_DIR}")
file(MAKE_DIRECTORY "${PACKAGE_DIR}")
run_checked(/usr/bin/ditto --norsrc --noextattr --noqtn --noacl
            "${X86_BUNDLE}" "${STAGE}")
run_checked(/usr/bin/lipo -create "${X86_BINARY}" "${ARM_BINARY}"
            -output "${STAGE_BINARY}")
run_checked(/usr/bin/xattr -cr "${STAGE}")
run_checked(/usr/bin/codesign --force --deep --sign - "${STAGE}")
run_checked(/usr/bin/codesign --verify --deep --strict --verbose=2 "${STAGE}")

execute_process(COMMAND /usr/bin/lipo -archs "${STAGE_BINARY}"
                OUTPUT_VARIABLE ARCHS OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT ARCHS MATCHES "arm64" OR NOT ARCHS MATCHES "x86_64")
  message(FATAL_ERROR "Universal VST3 has unexpected architectures: ${ARCHS}")
endif()

set(X86_THIN "${PACKAGE_DIR}/verify-x86_64")
set(ARM_THIN "${PACKAGE_DIR}/verify-arm64")
run_checked(/usr/bin/lipo "${STAGE_BINARY}" -thin x86_64 -output "${X86_THIN}")
run_checked(/usr/bin/lipo "${STAGE_BINARY}" -thin arm64 -output "${ARM_THIN}")
execute_process(COMMAND /usr/bin/otool -l "${X86_THIN}"
                OUTPUT_VARIABLE X86_LOAD_COMMANDS)
execute_process(COMMAND /usr/bin/otool -l "${ARM_THIN}"
                OUTPUT_VARIABLE ARM_LOAD_COMMANDS)
if(NOT X86_LOAD_COMMANDS MATCHES "version 10\\.13")
  message(FATAL_ERROR "x86_64 slice does not target macOS 10.13")
endif()
if(NOT ARM_LOAD_COMMANDS MATCHES "minos 11\\.0" AND
   NOT ARM_LOAD_COMMANDS MATCHES "version 11\\.0")
  message(FATAL_ERROR "arm64 slice does not target macOS 11")
endif()
file(REMOVE "${X86_THIN}" "${ARM_THIN}")

file(RENAME "${STAGE}" "${FINAL}")
file(MAKE_DIRECTORY "${RESOURCES}")
configure_file("${RESOURCE_README}" "${RESOURCES}/README.txt" COPYONLY)
configure_file("${INSTALLER}" "${INSTALL_COMMAND}" COPYONLY)
run_checked(/bin/chmod 755 "${INSTALL_COMMAND}")
if(DEFINED THIRD_PARTY_NOTICES AND EXISTS "${THIRD_PARTY_NOTICES}")
  configure_file("${THIRD_PARTY_NOTICES}"
                 "${PACKAGE_DIR}/THIRD_PARTY_NOTICES.md" COPYONLY)
  list(APPEND PACKAGE_ITEMS "THIRD_PARTY_NOTICES.md")
endif()
if(DEFINED JUCE_LICENSE AND EXISTS "${JUCE_LICENSE}")
  configure_file("${JUCE_LICENSE}" "${PACKAGE_DIR}/JUCE-LICENSE.md" COPYONLY)
  list(APPEND PACKAGE_ITEMS "JUCE-LICENSE.md")
endif()
execute_process(
  COMMAND /usr/bin/zip -qry -X "${ARCHIVE}" ${PACKAGE_ITEMS}
  WORKING_DIRECTORY "${PACKAGE_DIR}"
  RESULT_VARIABLE ZIP_RESULT)
if(NOT ZIP_RESULT EQUAL 0)
  message(FATAL_ERROR "zip packaging failed (${ZIP_RESULT})")
endif()
file(REMOVE_RECURSE "${FINAL}" "${RESOURCES}")
file(REMOVE "${INSTALL_COMMAND}")
file(REMOVE "${PACKAGE_DIR}/THIRD_PARTY_NOTICES.md")
file(REMOVE "${PACKAGE_DIR}/JUCE-LICENSE.md")
message(STATUS "Verified universal package: ${ARCHIVE} (${ARCHS})")
