set(LLVM_LINK_COMPONENTS
  CodeGenTypes
  Demangle
  Support
  TableGen
)

add_tablegen(mlir-tblgen MLIR
  DESTINATION "${MLIR_TOOLS_INSTALL_DIR}"
  EXPORT MLIR
  AttrOrTypeDefGen.cpp
  AttrOrTypeFormatGen.cpp
  BytecodeDialectGen.cpp
  DialectGen.cpp
  DirectiveCommonGen.cpp
  EnumsGen.cpp
  EnumPythonBindingGen.cpp
  FormatGen.cpp
  LLVMIRConversionGen.cpp
  LLVMIRIntrinsicGen.cpp
  mlir-tblgen.cpp
  OmpOpGen.cpp
  OpClass.cpp
  OpDefinitionsGen.cpp
  OpDocGen.cpp
  OpFormatGen.cpp
  OpGenHelpers.cpp
  OpInterfacesGen.cpp
  OpPythonBindingGen.cpp
  PassCAPIGen.cpp
  PassDocGen.cpp
  PassGen.cpp
  RewriterGen.cpp
  SPIRVUtilsGen.cpp
  TosaUtilsGen.cpp
  CppGenUtilities.cpp
  )

target_link_libraries(mlir-tblgen
  PRIVATE
  MLIRTblgenLib)

mlir_check_all_link_libraries(mlir-tblgen)
