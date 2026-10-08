cmake_minimum_required(VERSION 3.10)
project(oXitipLen CXX)

# The parser uses the C++ variant API of bison, which needs bison >= 3.0.
# On macOS the system bison is 2.3; point BISON_EXECUTABLE at a newer one,
# e.g. -DBISON_EXECUTABLE=$(brew --prefix bison)/bin/bison
find_package(BISON 3.0 REQUIRED)
find_package(FLEX REQUIRED)

# GLPK doesn't ship a CMake package, so look it up directly. Pass
# -DCMAKE_PREFIX_PATH=... if it is installed in a non-standard location
# (e.g. $(brew --prefix) on macOS).
find_path(GLPK_INCLUDE_DIR glpk.h)
find_library(GLPK_LIBRARY glpk)
if (NOT GLPK_INCLUDE_DIR OR NOT GLPK_LIBRARY)
    message(FATAL_ERROR "GLPK not found. Install it (e.g. 'apt-get install "
                        "libglpk-dev' or 'brew install glpk') and/or set "
                        "CMAKE_PREFIX_PATH.")
endif ()

BISON_TARGET (citip_parser parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cxx
              DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/parser.hxx)
FLEX_TARGET  (citip_scanner scanner.l ${CMAKE_CURRENT_BINARY_DIR}/scanner.cxx
              DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/scanner.hxx)
ADD_FLEX_BISON_DEPENDENCY(citip_scanner citip_parser)


add_executable(oXitipLen
    main.cpp
    citip.cpp
    ${BISON_citip_parser_OUTPUTS}
    ${FLEX_citip_scanner_OUTPUTS}
)
target_include_directories(oXitipLen PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FLEX_INCLUDE_DIRS}
    ${GLPK_INCLUDE_DIR}
)
set_property(TARGET oXitipLen PROPERTY CXX_STANDARD 11)
target_link_libraries(oXitipLen ${GLPK_LIBRARY})
