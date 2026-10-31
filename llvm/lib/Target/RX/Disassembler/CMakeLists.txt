add_llvm_component_library(LLVMRXDisassembler
  RXDisassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  MC
  RXInfo
  Support

  ADD_TO_COMPONENT
  RX
  )
