add_llvm_component_group(RX)

set(LLVM_TARGET_DEFINITIONS RX.td)

tablegen(LLVM RXGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM RXGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM RXGenCallingConv.inc -gen-callingconv)
tablegen(LLVM RXGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM RXGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM RXGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM RXGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM RXGenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM RXGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(RXCommonTableGen)

add_llvm_target(RXCodeGen
  RXAsmPrinter.cpp
  RXFrameLowering.cpp
  RXInstrInfo.cpp
  RXISelDAGToDAG.cpp
  RXISelLowering.cpp
  RXMCInstLower.cpp
  RXRegisterInfo.cpp
  RXSelectionDAGInfo.cpp
  RXSubtarget.cpp
  RXTargetMachine.cpp

  LINK_COMPONENTS
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  RXDesc
  RXInfo
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  RX
  )

add_subdirectory(AsmParser)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
