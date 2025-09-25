# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\demo_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\demo_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\profiler_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\profiler_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\untitled2_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\untitled2_autogen.dir\\ParseCache.txt"
  "demo_autogen"
  "profiler_autogen"
  "untitled2_autogen"
  )
endif()
