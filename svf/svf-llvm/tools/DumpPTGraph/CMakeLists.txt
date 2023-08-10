if(DEFINED IN_SOURCE_BUILD)
  add_llvm_tool(dump-ptg.cpp)
else()
  add_executable(dump-ptg dump-ptg.cpp)

  target_link_libraries(dump-ptg SvfLLVM ${llvm_libs})

  set_target_properties(dump-ptg PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                          ${CMAKE_BINARY_DIR}/bin)
endif()
