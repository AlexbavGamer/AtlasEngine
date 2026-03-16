# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "F:/VulkanEngineV2/deps/src/stb")
  file(MAKE_DIRECTORY "F:/VulkanEngineV2/deps/src/stb")
endif()
file(MAKE_DIRECTORY
  "F:/VulkanEngineV2/deps/build/stb"
  "F:/VulkanEngineV2/build/_deps/stb-subbuild/stb-populate-prefix"
  "F:/VulkanEngineV2/build/_deps/stb-subbuild/stb-populate-prefix/tmp"
  "F:/VulkanEngineV2/build/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp"
  "F:/VulkanEngineV2/build/_deps/stb-subbuild/stb-populate-prefix/src"
  "F:/VulkanEngineV2/build/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "F:/VulkanEngineV2/build/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "F:/VulkanEngineV2/build/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
