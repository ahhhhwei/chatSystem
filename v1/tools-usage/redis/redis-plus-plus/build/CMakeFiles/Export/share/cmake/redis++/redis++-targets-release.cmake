#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "redis++::redis++_static" for configuration "Release"
set_property(TARGET redis++::redis++_static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(redis++::redis++_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libredis++.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS redis++::redis++_static )
list(APPEND _IMPORT_CHECK_FILES_FOR_redis++::redis++_static "${_IMPORT_PREFIX}/lib/libredis++.a" )

# Import target "redis++::redis++" for configuration "Release"
set_property(TARGET redis++::redis++ APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(redis++::redis++ PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libredis++.so.1.3.15"
  IMPORTED_SONAME_RELEASE "libredis++.so.1"
  )

list(APPEND _IMPORT_CHECK_TARGETS redis++::redis++ )
list(APPEND _IMPORT_CHECK_FILES_FOR_redis++::redis++ "${_IMPORT_PREFIX}/lib/libredis++.so.1.3.15" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
