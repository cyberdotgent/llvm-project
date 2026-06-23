add_llvm_component_library(LLVMOS400MIInfo
  OS400MITargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  OS400MI
  )
