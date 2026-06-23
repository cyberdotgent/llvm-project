add_llvm_component_group(OS400MI)

add_llvm_target(OS400MICodeGen
  OS400MITargetMachine.cpp

  LINK_COMPONENTS
  CodeGen
  Core
  OS400MIDesc
  Support
  Target

  ADD_TO_COMPONENT
  OS400MI
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
