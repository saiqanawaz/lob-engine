include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.12.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(_core bindings.cpp)
target_link_libraries(_core PRIVATE lob)

# scikit-build-core sets CMAKE_INSTALL_PREFIX to the wheel staging dir;
# the module lands inside the `lob` package.
install(TARGETS _core LIBRARY DESTINATION lob)
