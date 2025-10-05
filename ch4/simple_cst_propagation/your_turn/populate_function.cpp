#include "llvm/ADT/PostOrderIterator.h" // For ReversePostOrderTraversal.
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

#ifndef let
#define let const auto
#else
#error "`let` already defined!"
#endif

#ifndef var
#define var auto
#else
#error "`var` already defined!"
#endif

template <typename Evaluator>
Value *evalBinaryInstruction(Instruction &instr, const Evaluator &eval) {
  let *rand1 = dyn_cast<ConstantInt>(instr.getOperand(0));
  let *rand2 = dyn_cast<ConstantInt>(instr.getOperand(1));
  if (!rand1 || !rand2) {
    return nullptr;
  }
  return eval(rand1->getValue(), rand2->getValue());
}

// Takes \p Fn and apply a simple constant propagation optimization.
// \returns true if \p Fn was modified (i.e., something had been constant
// propagated), false otherwise.
bool myConstantPropagation(Function &Fn) {
  LLVMContext &context = Fn.getParent()->getContext();
  var changed = false;
  var converged = false;
  while (!converged) {
    converged = true;
    var rpot = ReversePostOrderTraversal<Function *>(&Fn);
    for (BasicBlock *bb : rpot) {
      for (Instruction &instr : make_early_inc_range(*bb)) {
        if (!instr.isBinaryOp()) {
          continue;
        }
        Value *constant = nullptr;
        let opcode = instr.getOpcode();
        switch (opcode) {
        case Instruction::Add:
          constant = evalBinaryInstruction(
              instr, [&context](const APInt &r1, const APInt &r2) -> Value * {
                return ConstantInt::get(context, r1 + r2);
              });
          break;
        case Instruction::Sub:
          constant = evalBinaryInstruction(
              instr, [&context](const APInt &r1, const APInt &r2) -> Value * {
                return ConstantInt::get(context, r1 - r2);
              });
          break;
        case Instruction::And:
          constant = evalBinaryInstruction(
              instr, [&context](const APInt &r1, const APInt &r2) -> Value * {
                return ConstantInt::get(context, r1 & r2);
              });
          break;
        case Instruction::Or:
          constant = evalBinaryInstruction(
              instr, [&context](const APInt &r1, const APInt &r2) -> Value * {
                return ConstantInt::get(context, r1 | r2);
              });
          break;
        case Instruction::Xor:
          constant = evalBinaryInstruction(
              instr, [&context](const APInt &r1, const APInt &r2) -> Value * {
                return ConstantInt::get(context, r1 ^ r2);
              });
          break;
        case Instruction::Mul:
          constant = evalBinaryInstruction(
              instr, [&context](const APInt &r1, const APInt &r2) -> Value * {
                return ConstantInt::get(context, r1 * r2);
              });
          break;
        case Instruction::SDiv:
          constant = evalBinaryInstruction(
              instr,
              [&context, &instr](const APInt &r1, const APInt &r2) -> Value * {
                if (r2 == 0) {
                  return PoisonValue::get(instr.getType());
                }
                return ConstantInt::get(context, r1.sdiv(r2));
              });
          break;
        case Instruction::UDiv:
          constant = evalBinaryInstruction(
              instr,
              [&context, &instr](const APInt &r1, const APInt &r2) -> Value * {
                if (r2 == 0) {
                  return PoisonValue::get(instr.getType());
                }
                return ConstantInt::get(context, r1.udiv(r2));
              });
          break;
        case Instruction::LShr:
          constant = evalBinaryInstruction(
              instr, [&context](const APInt &r1, const APInt &r2) -> Value * {
                return ConstantInt::get(context, r1.lshr(r2));
              });
          break;
        case Instruction::Shl:
          constant = evalBinaryInstruction(
              instr, [&context](const APInt &r1, const APInt &r2) -> Value * {
                return ConstantInt::get(context, r1.shl(r2));
              });
          break;
        case Instruction::AShr:
          constant = evalBinaryInstruction(
              instr, [&context](const APInt &r1, const APInt &r2) -> Value * {
                return ConstantInt::get(context, r1.ashr(r2));
              });
          break;
        case Instruction::SRem:
          constant = evalBinaryInstruction(
              instr,
              [&context, &instr](const APInt &r1, const APInt &r2) -> Value * {
                if (r2 == 0) {
                  return PoisonValue::get(instr.getType());
                }
                return ConstantInt::get(context, r1.srem(r2));
              });
          break;
        case Instruction::URem:
          constant = evalBinaryInstruction(
              instr,
              [&context, &instr](const APInt &r1, const APInt &r2) -> Value * {
                if (r2 == 0) {
                  return PoisonValue::get(instr.getType());
                }
                return ConstantInt::get(context, r1.urem(r2));
              });
          break;
        default:
          continue;
        }
        if (constant) {
          instr.replaceAllUsesWith(constant);
          instr.eraseFromParent();
          converged = false;
          changed = true;
        }
      }
    }
  }
  return changed;
}
#undef let
#undef var
