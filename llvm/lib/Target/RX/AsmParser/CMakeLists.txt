add_llvm_component_library(LLVMRXAsmParser
  RXAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  RXDesc
  RXInfo
  Support

  ADD_TO_COMPONENT
  RX
  )
