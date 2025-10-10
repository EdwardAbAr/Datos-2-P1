# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\demo0_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\demo0_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\demo1_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\demo1_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\profiler_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\profiler_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\untitled2_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\untitled2_autogen.dir\\ParseCache.txt"
  "demo0_autogen"
  "demo1_autogen"
  "profiler_autogen"
  "untitled2_autogen"
  )
endif()
