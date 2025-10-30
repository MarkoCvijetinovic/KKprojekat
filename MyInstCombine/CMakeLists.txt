add_llvm_library(MyInstCombine MODULE
  MyInstCombine.cpp

  DEPENDS
  intrinsics_gen

  PLUGIN_TOOL 
  opt

  LINK_COMPONENTS
  Analysis
  Core
  Support
  TransformUtils
  )

