########################################################################
# Header and library resources needed to communicate with the device.
# These may be found within the build tree or in an external project.
########################################################################
set(OSMO_FL2K_INCLUDE_DIRS ../)
set(OSMO_FL2K_LIBRARIES libosmo-fl2k_static)

########################################################################
# build the module
########################################################################
find_package(SoapySDR CONFIG REQUIRED)

if (NOT SoapySDR_FOUND)
    message(WARNING "SoapySDR development files not found - skipping support")
    return()
endif ()

include_directories(${OSMO_FL2K_INCLUDE_DIRS})
SOAPY_SDR_MODULE_UTIL(
    TARGET OsmoFL2K
    SOURCES Registration.cpp Settings.cpp SampleRates.cpp Streaming.cpp
    LIBRARIES ${OSMO_FL2K_LIBRARIES}
)

add_executable(Test Test.cpp)
target_link_libraries(Test ${SoapySDR_LIBRARIES})

########################################################################
# Debug macro for printing all the variables
########################################################################
macro(print_all_variables)
    message(STATUS "print_all_variables------------------------------------------{")
    get_cmake_property(_variableNames VARIABLES)
    foreach (_variableName ${_variableNames})
        message(STATUS "${_variableName}=${${_variableName}}")
    endforeach()
    message(STATUS "print_all_variables------------------------------------------}")
endmacro()

#print_all_variables()
