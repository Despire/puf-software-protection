#include "PufParser.h"

#include <fstream>
#include <sys/fcntl.h>

#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

static llvm::cl::opt<std::string> EnrollmentFile(
        "enrollment",
        llvm::cl::desc("Enrollment json file that was generated"),
        llvm::cl::value_desc("string"),
        llvm::cl::Required
);

llvm::PreservedAnalyses PufParser::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
    auto &ctx = M.getContext();

    // Create global variable for the file descriptor
    auto *fdGlobal = new llvm::GlobalVariable(
            M,
            LLVM_I32(ctx),
            false,
            llvm::GlobalValue::InternalLinkage,
            LLVM_CONST_I32(ctx, 0), "puf_fd"
    );

    // open the /dev/puf in a ctor
    puf_open_ctor(M, fdGlobal);
    // close /dev/puf in a dtor
    puf_close_dtor(M, fdGlobal);

    return llvm::PreservedAnalyses::none();
}

bool PufParser::puf_open_ctor(llvm::Module &M, llvm::GlobalVariable *Fd) {
    using namespace llvm;
    auto &ctx = M.getContext();

    // Prepare the Open function.
    auto *function_type = FunctionType::get(
            LLVM_I32(ctx),
            {
                    Type::getInt8PtrTy(ctx),
                    LLVM_I32(ctx)
            },
            false
    );

    FunctionCallee open_func = M.getOrInsertFunction("open", function_type);

    // Create function for opening the puf.
    function_type = FunctionType::get(Type::getVoidTy(ctx), false);
    auto *puf_func = Function::Create(function_type, Function::InternalLinkage, "____open_device____", &M);

    // fill body of the function.
    auto *BB = BasicBlock::Create(ctx, "entry", puf_func);
    IRBuilder<> Builder(BB);
    auto *fd = Builder.CreateCall(open_func, {Builder.CreateGlobalStringPtr("/dev/puf"), Builder.getInt32(O_RDWR)});

    // error handle the file descriptor.
    auto *is_not_open_fd = Builder.CreateICmpSLT(fd, Builder.getInt32(0));
    auto *failed_bb = BasicBlock::Create(ctx, "error", puf_func);
    auto *success_bb = BasicBlock::Create(ctx, "continue", puf_func);

    Builder.CreateCondBr(is_not_open_fd, failed_bb, success_bb);

    // Handle the error case first, by exiting the program.
    Builder.SetInsertPoint(failed_bb);
    auto exit_func = M.getOrInsertFunction("exit", FunctionType::get(
            Type::getVoidTy(ctx),
            {LLVM_I32(ctx)},
            false)
    );
    Builder.CreateCall(exit_func, {Builder.getInt32(DEV_FAIL)});
    Builder.CreateUnreachable();

    // handle ok path.
    Builder.SetInsertPoint(success_bb);
    Builder.CreateStore(fd, Fd);
    Builder.CreateRetVoid();

    appendToGlobalCtors(M, puf_func, 0);

    return true;
}

bool PufParser::puf_close_dtor(llvm::Module &M, llvm::GlobalVariable *Fd) {
    using namespace llvm;
    auto &ctx = M.getContext();

    // prepare close function.
    auto *function_type = FunctionType::get(
            LLVM_I32(ctx),
            {LLVM_I32(ctx)},
            false
    );

    FunctionCallee close_func = M.getOrInsertFunction("close", function_type);

    // create close func.
    function_type = FunctionType::get(Type::getVoidTy(ctx), false);
    auto *puf_func = Function::Create(function_type, Function::InternalLinkage, "____close_device____", &M);

    auto *BB = BasicBlock::Create(ctx, "entry", puf_func);
    IRBuilder<> Builder(BB);
    auto *fd = Builder.CreateLoad(Fd->getValueType(), Fd);
    Builder.CreateCall(close_func, {fd});
    Builder.CreateRetVoid();

    appendToGlobalDtors(M, puf_func, 0);

    return true;
}

EnrollData PufParser::read_enrollment_data() {
    llvm::outs() << "input enrollment file: " << EnrollmentFile << "\n";
    std::ifstream inputFile(EnrollmentFile);
    if (!inputFile.is_open()) {
        llvm::errs() << "Could not open the file." << "\n";
        throw std::runtime_error("failed to open file");
    }

    nlohmann::json j;
    inputFile >> j;

    return j.get<EnrollData>();
}

//------------------------------------------------------
//               Registration of the Plugin
//------------------------------------------------------
llvm::PassPluginLibraryInfo getPufParserPluginInfo() {
    return {
            LLVM_PLUGIN_API_VERSION,
            "pufparser",
            LLVM_VERSION_STRING,
            [](llvm::PassBuilder &PB) {
                using namespace llvm;
                PB.registerPipelineParsingCallback(
                        [&](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
                            if (Name == "pufparser") {
                                MPM.addPass(PufParser());
                                return true;
                            }
                            return false;
                        });
            }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getPufParserPluginInfo();
}
