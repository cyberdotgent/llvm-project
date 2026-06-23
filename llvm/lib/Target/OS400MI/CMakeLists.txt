add_llvm_component_group(OS400MI)

# This target is intentionally skeletal. Code generation will be added only
# after the symbolic MI source model and ABI choices are documented.
add_llvm_target(OS400MICodeGen
  OS400MITargetMachine.cpp

  LINK_COMPONENTS
  Core
  Support
  Target

  ADD_TO_COMPONENT
  OS400MI
  )

add_subdirectory(TargetInfo)
