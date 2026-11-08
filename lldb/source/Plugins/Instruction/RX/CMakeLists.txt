add_lldb_library(lldbPluginInstructionRX PLUGIN
  EmulateInstructionRX.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbCore
    lldbInterpreter
    lldbSymbol
  )
