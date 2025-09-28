#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"    // For ConstantInt.
#include "llvm/IR/DerivedTypes.h" // For PointerType, FunctionType.
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h" // For errs().

#include <memory> // For unique_ptr

using namespace llvm;

// Turn C++ into a tolerable language
#ifndef let
#define let const auto
#else
#error "Who TF defined `let` in the preprocessing"
#endif

#ifndef var
#define var auto
#else
#error "Who TF defined `var` in the preprocessing"
#endif

// The goal of this function is to build a Module that
// represents the lowering of the following foo, a C function:
// extern int baz();
// extern void bar(int);
// void foo(int a, int b) {
//   int var = a + b;
//   if (var == 0xFF) {
//     bar(var);
//     var = baz();
//   }
//   bar(var);
// }
//
// The IR for this snippet (at O0) is:
// define void @foo(i32 %arg, i32 %arg1) {
// bb:
//   %i = alloca i32
//   %i2 = alloca i32
//   %i3 = alloca i32
//   store i32 %arg, ptr %i
//   store i32 %arg1, ptr %i2
//   %i4 = load i32, ptr %i
//   %i5 = load i32, ptr %i2
//   %i6 = add i32 %i4, %i5
//   store i32 %i6, ptr %i3
//   %i7 = load i32, ptr %i3
//   %i8 = icmp eq i32 %i7, 255
//   br i1 %i8, label %bb9, label %bb12
//
// bb9:
//   %i10 = load i32, ptr %i3
//   call void @bar(i32 %i10)
//   %i11 = call i32 @baz()
//   store i32 %i11, ptr %i3
//   br label %bb12
//
// bb12:
//   %i13 = load i32, ptr %i3
//   call void @bar(i32 %i13)
//   ret void
// }
//
// declare void @bar(i32)
// declare i32 @baz(...)
std::unique_ptr<Module> myBuildModule(LLVMContext &Ctxt) {
  // construct module
  std::unique_ptr<Module> buildModule = std::make_unique<Module>("Build Module", Ctxt);
  // declare primitive types to be used
  let intType = Type::getInt32Ty(Ctxt);
  let voidType = Type::getVoidTy(Ctxt);
  let ptr = PointerType::get(Ctxt, 0u);

  // insert declaration of `bar` in the module
  Type *fnBarArgs[1] = {intType};
  var fnBarType = FunctionType::get(voidType, ArrayRef(fnBarArgs), false);
  var barCallee = buildModule->getOrInsertFunction("bar", fnBarType);
  // insert declaration of `baz` in the module
  var fnBazType = FunctionType::get(intType, false);
  var bazCallee = buildModule->getOrInsertFunction("baz", fnBazType);
  // insert declaration of `foo` in the module
  Type *fnFooArgs[2] = {intType, intType};
  var fnFooType = FunctionType::get(voidType, ArrayRef(fnFooArgs), false);
  var fooCallee = buildModule->getOrInsertFunction("foo", fnFooType);

  // populate body of `foo` with basic blocks
  var fnFoo = cast<Function>(fooCallee.getCallee());
  // Allocate basic blocks
  var bb = BasicBlock::Create(Ctxt, "bb", fnFoo);
  var bb9 = BasicBlock::Create(Ctxt, "bb9", fnFoo);
  var bb12 = BasicBlock::Create(Ctxt, "bb12", fnFoo);
  // -> populate `bb`
  IRBuilder builder(bb);
  Value *i = builder.CreateAlloca(intType);
  Value *i2 = builder.CreateAlloca(intType);
  Value *i3 = builder.CreateAlloca(intType);
  Value *a = fnFoo->getArg(0);
  Value *b = fnFoo->getArg(1);	
  builder.CreateStore(a, i);
  builder.CreateStore(b, i2);
  Value *i4 = builder.CreateLoad(intType, i);
  Value *i5 = builder.CreateLoad(intType, i2);
  Value *i6 = builder.CreateAdd(i4, i5);
  builder.CreateStore(i6, i3);
  Value *i7 = builder.CreateLoad(intType, i3);
  Value *i8 = builder.CreateCmp(CmpInst::ICMP_EQ, i7, ConstantInt::get(intType, 255));
  builder.CreateCondBr(i8, bb9, bb12);
  // -> populate bb9
  builder.SetInsertPoint(bb9);
  Value *i10 = builder.CreateLoad(intType, i3);
  Value *barArgs[1] = { i10 };
  builder.CreateCall(fnBarType, barCallee.getCallee(), ArrayRef(barArgs));
  Value *i11 = builder.CreateCall(fnBazType, bazCallee.getCallee());
  builder.CreateStore(i11, i3);
  builder.CreateBr(bb12);
  // -> populate bb12
  builder.SetInsertPoint(bb12);
  Value *i13 = builder.CreateLoad(intType, i3);
  builder.CreateCall(fnBarType, barCallee.getCallee(), ArrayRef({i13}));
  builder.CreateRetVoid();
  return buildModule;
}

/* Notes
 * Required APIs:
 *   `Module::getOrInsertFunction`
 *   `FunctionType::get`
 *   `Type::VoidTy(LLVMContext&)`
 *   `Type::getInt32Ty(LLVMContext&)`
 *   `Function::Create`
 *   `BasicBlock::Create`
 * */

#undef let
#undef var
