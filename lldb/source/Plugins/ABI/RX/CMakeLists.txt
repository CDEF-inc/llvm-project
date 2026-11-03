add_lldb_library(lldbPluginABIRX PLUGIN
  ABISysV_rx.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )
