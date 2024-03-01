#include <vector>

#include "Checksum.h"
#include "OpaquePredicates.h"

#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Linker/Linker.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

llvm::PreservedAnalyses Checksum::run(llvm::Module &M, llvm::ModuleAnalysisManager &) {
    std::string funcName;
    link_with_hash(M, funcName);
    auto &ctx = M.getContext();

    std::vector<llvm::Function*> funcs;
    for (auto &f : M ) {
        if (f.isDeclaration()) continue;
        if (f.empty()) continue;
        funcs.push_back(&f);
    }

    for (auto &func: funcs) {
        llvm::InlineAsm *parity = llvm::InlineAsm::get(
                llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
                R"(.byte 00, 01, 02, 03)",
                "",
                true
        );

        if (func->getName() == funcName) continue;

        // TODO: improve
        {
            llvm::IRBuilder<> builder(&*func->getEntryBlock().getFirstInsertionPt());
            auto cast = builder.CreatePtrToInt(func, LLVM_I64(ctx));

            auto *call = builder.CreateCall(M.getFunction(funcName), {
                    cast,
                    LLVM_CONST_I32(ctx, 10),
                    LLVM_CONST_I32(ctx, 3)
            });

            llvm::InlineFunctionInfo ifi;
            auto inlineop = llvm::InlineFunction(*call, ifi);
            if (!inlineop.isSuccess()) {
                llvm::outs() << inlineop.getFailureReason() << '\n';
            }

            llvm::BasicBlock &SplitBlock = *RandomElement(func->begin(), func->end());
            llvm::Instruction *SplitInstruction = RandomNonPHIInstruction(SplitBlock);
            llvm::Value *RandomFunc = *RandomElement(funcs.begin(), funcs.end());

            builder.SetInsertPoint(SplitInstruction);
            llvm::Value *RandomInteger = builder.CreatePtrToInt(RandomFunc, LLVM_I64(ctx));

            auto *predicate = OpaquePredicates::getRandomOpaquelyTruePredicate();
            llvm::Value *condition = (&*predicate)(RandomInteger, SplitInstruction);
            auto newBB = llvm::SplitBlockAndInsertIfThen(condition, SplitInstruction, false);

            // since we're using a true predicate swap the branch instruction successor.
            llvm::cast<llvm::BranchInst>(SplitBlock.getTerminator())->swapSuccessors();

            builder.SetInsertPoint(newBB);
            builder.CreateCall(parity, {});

            llvm::outs() << *func << "\n";
        }
    }

    return llvm::PreservedAnalyses::all();
}

void Checksum::link_with_hash(llvm::Module &M, std::string &funcName) noexcept {
    const std::string func = R"(
@.str = private unnamed_addr constant [5 x i8] c"%02X\00", align 1

define i32 @hash5(i64 %0, i32 %1, i32 %2) {
  %4 = icmp eq i32 %1, 0
  br i1 %4, label %20, label %5

5:                                                ; preds = %3, %5
  %6 = phi i32 [ %16, %5 ], [ 0, %3 ]
  %7 = phi i64 [ %17, %5 ], [ %0, %3 ]
  %8 = phi i32 [ %18, %5 ], [ %1, %3 ]
  %9 = inttoptr i64 %7 to i8*
  %10 = load i8, i8* %9, align 1
  %11 = zext i8 %10 to i32
  %12 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([5 x i8], [5 x i8]* @.str, i64 0, i64 0), i32 %11)
  %13 = load i8, i8* %9, align 1
  %14 = zext i8 %13 to i32
  %15 = add i32 %6, %14
  %16 = mul i32 %15, %2
  %17 = add i64 %7, 1
  %18 = add i32 %8, -1
  %19 = icmp eq i32 %18, 0
  br i1 %19, label %20, label %5

20:                                               ; preds = %5, %3
  %21 = phi i32 [ 0, %3 ], [ %16, %5 ]
  ret i32 %21
}

declare noundef i32 @printf(i8* nocapture noundef readonly, ...)
)";

    llvm::SMDiagnostic error;
    auto funcBuffer = llvm::MemoryBuffer::getMemBuffer(func, "hashFunc", false);
    auto decodeModule = llvm::parseIR(funcBuffer->getMemBufferRef(), error, M.getContext());
    if (decodeModule == nullptr) {
        llvm::errs() << error.getMessage() << '\n';;
        return;
    }

    funcName = "hash" + std::to_string(RandomInt64());
    decodeModule->getFunction("hash5")->setName(funcName);

    llvm::Linker linker(M);
    if (linker.linkInModule(std::move(decodeModule))) {
        llvm::errs() << "failed to link modules" << '\n';
        return;
    }
}

//------------------------------------------------------
//               Registration of the Plugin
//------------------------------------------------------
llvm::PassPluginLibraryInfo getChecksumPluginInfo() {
    return {
            LLVM_PLUGIN_API_VERSION,
            "checksum",
            LLVM_VERSION_STRING,
            [](llvm::PassBuilder &PB) {
                using namespace llvm;
                PB.registerPipelineParsingCallback(
                        [&](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
                            if (Name == "checksum") {
                                MPM.addPass(Checksum());
                                return true;
                            }
                            return false;
                        });
            }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getChecksumPluginInfo();
}


