#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h" // For CreateStackObject.
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h" // For MachinePointerInfo.
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetOpcodes.h"     // For INLINEASM.
#include "llvm/CodeGenTypes/LowLevelType.h" // For LLT.
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h" // For ICMP_EQ.

using namespace llvm;

// Turn C++ into a tolerable language
#ifndef let
#define let const auto
#else
#error "Who TF defined `let`"
#endif

#ifndef var
#define var auto
#else
#error "Who TF defined `var`"
#endif


// The goal of this function is to build a MachineFunction that
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
// The proposed ABI is:
// - 32-bit arguments are passed through registers: w0, w1
// - 32-bit returned values are passed through registers: w0, w1
// w0 and w1 are given as argument of this Function.
//
// The local variable named var is expected to live on the stack.
MachineFunction *populateMachineIR(MachineModuleInfo &MMI, Function &Foo,
                                   Register W0, Register W1) {
  MachineFunction &MF = MMI.getOrCreateMachineFunction(Foo);

  // The type for bool.
  LLT I1 = LLT::scalar(1);
  // The type of var.
  LLT I32 = LLT::scalar(32);

  // To use to create load and store for var.
  MachinePointerInfo PtrInfo;
  Align VarStackAlign(4);

  // The type for the address of var.
  LLT VarAddrLLT = LLT::pointer(/*AddressSpace=*/0, /*SizeInBits=*/64);

  // The stack slot for var.
  int FrameIndex = MF.getFrameInfo().CreateStackObject(32, VarStackAlign,
                                                       /*IsSpillSlot=*/false);
  // Create BBs
  var *entryBB = MF.CreateMachineBasicBlock();
  MF.push_back(entryBB);
  var *bb1 = MF.CreateMachineBasicBlock();
  MF.push_back(bb1);
  var *bb2 = MF.CreateMachineBasicBlock();
  MF.push_back(bb2);
  // build CFG
  entryBB->addSuccessor(bb1);
  entryBB->addSuccessor(bb2);
  bb1->addSuccessor(bb2);
  // build the IR, one BB at a time
  // entryBB
  var builder = MachineIRBuilder(*entryBB, entryBB->end());
  // input operands
  Register A = builder.buildCopy(I32, W0).getReg(0);
  Register B = builder.buildCopy(I32, W1).getReg(0);
  // allocate variable on stack for a + b
  Register addABStackAddr = builder.buildFrameIndex(VarAddrLLT, FrameIndex).getReg(0);
  // add
  Register addResult = builder.buildAdd(I32, A, B).getReg(0);
  // store result in stack
  builder.buildStore(addResult, addABStackAddr, PtrInfo, VarStackAlign);
  // compare add result with constant
  Register const0xFF = builder.buildConstant(I32, 0xFF).getReg(0);
  addResult = builder.buildLoad(I32, addABStackAddr, PtrInfo, VarStackAlign).getReg(0);
  Register cmp = builder.buildICmp(CmpInst::ICMP_EQ, I1, addResult, const0xFF).getReg(0);
  // cond br
  builder.buildBrCond(cmp, *bb1);
  builder.buildBr(*bb2);

  // bb1
  builder.setInsertPt(*bb1, bb1->end());
  addResult = builder.buildLoad(I32, addABStackAddr, PtrInfo, VarStackAlign).getReg(0);
  // arm ABI calling convention
  builder.buildCopy(W0, addResult);
  builder.buildInstr(TargetOpcode::INLINEASM, {}, {})
		.addExternalSymbol("bl @bar")
		.addImm(0)
		.addReg(W0, RegState::Implicit);
  builder.buildInstr(TargetOpcode::INLINEASM, {}, {})
		.addExternalSymbol("bl @baz")
		.addImm(0)
		.addReg(W0, RegState::Implicit | RegState::Define);
  Register bazResult = builder.buildCopy(I32, W0).getReg(0);
  builder.buildStore(bazResult, addABStackAddr, PtrInfo, VarStackAlign);
 
  // bb2
  builder.setInsertPt(*bb2, bb2->end());
  addResult = builder.buildLoad(I32, addABStackAddr, PtrInfo, VarStackAlign).getReg(0);
  builder.buildCopy(W0, addResult);
  builder.buildInstr(TargetOpcode::INLINEASM, {}, {})
		.addExternalSymbol("bl @bar")
		.addImm(0)
		.addReg(W0, RegState::Implicit);
  builder.buildInstr(TargetOpcode::INLINEASM, {}, {})
		.addExternalSymbol("ret")
		.addImm(0);
  return &MF;
}
#undef let
#undef var
