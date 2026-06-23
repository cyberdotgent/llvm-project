add_llvm_component_library(LLVMOS400MIDesc
  OS400MIMCTargetDesc.cpp

  LINK_COMPONENTS
  OS400MIInfo
  Support

  ADD_TO_COMPONENT
  OS400MI
  )
