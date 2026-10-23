add_llvm_component_library(LLVMRXDesc
  RXAsmBackend.cpp
  RXELFObjectWriter.cpp
  RXELFStreamer.cpp
  RXInstPrinter.cpp
  RXMCAsmInfo.cpp
  RXMCCodeEmitter.cpp
  RXMCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  RXInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  RX
  )
