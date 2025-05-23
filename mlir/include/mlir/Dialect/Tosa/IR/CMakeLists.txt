add_mlir_dialect(TosaOps tosa)
add_mlir_doc(TosaOps TosaOps Dialects/ -gen-op-doc)
add_mlir_interface(TosaInterfaces)

set(LLVM_TARGET_DEFINITIONS TosaOps.td)
mlir_tablegen(TosaOpsTypesBase.h.inc -gen-typedef-decls -typedefs-dialect=tosa)
mlir_tablegen(TosaOpsTypesBase.cpp.inc -gen-typedef-defs -typedefs-dialect=tosa)
mlir_tablegen(TosaAttributes.h.inc -gen-attrdef-decls -attrdefs-dialect=tosa)
mlir_tablegen(TosaAttributes.cpp.inc -gen-attrdef-defs -attrdefs-dialect=tosa)
add_public_tablegen_target(MLIRTosaAttributesIncGen)

set(LLVM_TARGET_DEFINITIONS TosaDialectBytecode.td)
mlir_tablegen(TosaDialectBytecode.cpp.inc -gen-bytecode -bytecode-dialect="Tosa")
add_public_tablegen_target(MLIRTosaDialectBytecodeIncGen)

set(LLVM_TARGET_DEFINITIONS TosaOpBase.td)
mlir_tablegen(TosaEnums.h.inc -gen-enum-decls)
mlir_tablegen(TosaEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(MLIRTosaEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS TosaOps.td)
mlir_tablegen(TosaAvailability.h.inc -gen-avail-interface-decls)
mlir_tablegen(TosaAvailability.cpp.inc -gen-avail-interface-defs)
mlir_tablegen(TosaOpAvailabilityImpl.inc -gen-tosa-avail-impls)
add_public_tablegen_target(MLIRTosaAvailabilityIncGen)
