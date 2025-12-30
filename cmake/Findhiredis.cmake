# Use pkg-config to find hiredis
find_package(PkgConfig QUIET)
pkg_check_modules(PC_HIREDIS QUIET hiredis)

# Find the include directory
find_path(hiredis_INCLUDE_DIR
    NAMES hiredis/hiredis.h
    HINTS ${PC_HIREDIS_INCLUDEDIR} ${PC_HIREDIS_INCLUDE_DIRS}
    PATH_SUFFIXES hiredis
)

# Find the library
find_library(hiredis_LIBRARY
    NAMES hiredis
    HINTS ${PC_HIREDIS_LIBDIR} ${PC_HIREDIS_LIBRARY_DIRS}
)

# Standard handle for find_package
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hiredis
    REQUIRED_VARS hiredis_LIBRARY hiredis_INCLUDE_DIR
    VERSION_VAR PC_HIREDIS_VERSION
)

if(hiredis_FOUND)
    set(hiredis_LIBRARIES ${hiredis_LIBRARY})
    set(hiredis_INCLUDE_DIRS ${hiredis_INCLUDE_DIR})

    # Create an imported target for modern CMake usage
    if(NOT TARGET hiredis::hiredis)
        add_library(hiredis::hiredis UNKNOWN IMPORTED)
        set_target_properties(hiredis::hiredis PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${hiredis_INCLUDE_DIRS}"
            IMPORTED_LOCATION "${hiredis_LIBRARIES}"
        )
    endif()
endif()

mark_as_advanced(hiredis_INCLUDE_DIR hiredis_LIBRARY)
