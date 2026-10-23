add_llvm_component_library(LLVMRXInfo
  RXTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  RX
  )
