if(NOT DEFINED SOURCE_BUNDLE OR NOT EXISTS "${SOURCE_BUNDLE}")
  message(FATAL_ERROR "SOURCE_BUNDLE does not name a built plug-in bundle")
endif()
if(NOT DEFINED PACKAGE_DIR)
  message(FATAL_ERROR "PACKAGE_DIR is required")
endif()

if(NOT DEFINED BUNDLE_EXTENSION)
  set(BUNDLE_EXTENSION "vst3")
endif()
if(NOT DEFINED ARCHIVE_NAME)
  set(ARCHIVE_NAME "Ensoniq-EPS-16-Plus-arm64.zip")
endif()

set(STAGE "${PACKAGE_DIR}/Ensoniq EPS-16 Plus.stage")
set(FINAL "${PACKAGE_DIR}/Ensoniq EPS-16 Plus.${BUNDLE_EXTENSION}")
set(RESOURCES "${PACKAGE_DIR}/EPS_files")
set(ARCHIVE "${PACKAGE_DIR}/${ARCHIVE_NAME}")
set(PACKAGE_ITEMS "Ensoniq EPS-16 Plus.${BUNDLE_EXTENSION}" "EPS_files")
file(REMOVE_RECURSE "${PACKAGE_DIR}")
file(MAKE_DIRECTORY "${PACKAGE_DIR}")

function(run_checked)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE RESULT)
  if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Command failed (${RESULT}): ${ARGV}")
  endif()
endfunction()

# Documents folders managed by macOS may attach Finder/file-provider metadata
# to a directory named *.vst3. Sign under a neutral staging suffix, archive
# without extended attributes, and let installation recreate a clean bundle.
run_checked(/usr/bin/ditto --norsrc --noextattr --noqtn --noacl
            "${SOURCE_BUNDLE}" "${STAGE}")
run_checked(/usr/bin/xattr -cr "${STAGE}")
run_checked(/usr/bin/codesign --force --deep --sign - "${STAGE}")
run_checked(/usr/bin/codesign --verify --deep --strict --verbose=2 "${STAGE}")
file(RENAME "${STAGE}" "${FINAL}")
file(MAKE_DIRECTORY "${RESOURCES}")
configure_file("${RESOURCE_README}" "${RESOURCES}/README.txt" COPYONLY)
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
  COMMAND /usr/bin/zip -qry -X "${ARCHIVE}"
          ${PACKAGE_ITEMS}
  WORKING_DIRECTORY "${PACKAGE_DIR}"
  RESULT_VARIABLE ZIP_RESULT
)
if(NOT ZIP_RESULT EQUAL 0)
  message(FATAL_ERROR "zip packaging failed (${ZIP_RESULT})")
endif()
file(REMOVE_RECURSE "${FINAL}")
file(REMOVE_RECURSE "${RESOURCES}")
file(REMOVE "${PACKAGE_DIR}/THIRD_PARTY_NOTICES.md")
file(REMOVE "${PACKAGE_DIR}/JUCE-LICENSE.md")
message(STATUS "Verified package: ${ARCHIVE}")
