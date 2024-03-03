#include <vector>
#include <fstream>
#include <sstream>

#include "Crossover.h"
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

llvm::InlineAsm *getasm(llvm::LLVMContext &ctx) {
    using namespace llvm;
    return llvm::InlineAsm::get(
        FunctionType::get(
                llvm::StructType::get(LLVM_I32(ctx), LLVM_I64(ctx), LLVM_I32(ctx)),
                {LLVM_I32(ctx), LLVM_I32(ctx), LLVM_I64(ctx), LLVM_I32(ctx)}, false),
    R"(1:
ldr r3, [$1], #4
mul $0, $3, $0
add $0, $0, r3
subs $2, $2, #1
bne 1b
)",
            "=r,=r,=r,r,0,1,2,~{r3},~{cc},~{memory}",
            true
    );
}

static llvm::cl::opt<std::string> OutputFile(
        "out",
        llvm::cl::desc("where the output json regarding functions will be written"),
        llvm::cl::value_desc("string"),
        llvm::cl::Optional
);

static llvm::cl::opt<std::string> InputFile(
        "in",
        llvm::cl::desc("from where data regarding function hashes will be read from."),
        llvm::cl::value_desc("string"),
        llvm::cl::Optional
);

static llvm::cl::opt<bool> SkipCalculatingParity(
        "pskip",
        llvm::cl::desc("If the parity placeholder should be replaced by the actual value"),
        llvm::cl::value_desc("bool"),
        llvm::cl::Optional
);


llvm::PreservedAnalyses Checksum::run(llvm::Module &M, llvm::ModuleAnalysisManager &) {
    std::vector<std::string> func_names;
    std::vector<llvm::Function *> funcs;

    for (auto &f: M) {
        if (f.isIntrinsic()) continue;
        if (f.isDeclaration()) continue;
        if (f.empty()) continue;
        funcs.push_back(&f);
        func_names.push_back(f.getName().str());
    }

//    std::string hashFunc;
//    link_with_hash(M, hashFunc);

    if (!OutputFile.empty()) {
        write_func_requests(OutputFile, func_names);
    }

    // If the inputfile is empty this will be a empty patch i.e the patch
    // will not contain actual values.

    // Table contains the functions which can be actually checksummed
    auto table = read_func_metadata(InputFile);
    bool emptyPatch = table.empty();

    if (emptyPatch) {
        llvm::outs() << "InputFile empty: patching empty" << '\n';
    }

    auto &ctx = M.getContext();
    for (auto &func: funcs) {
        if (!emptyPatch) { // each function we are going to patch should lbe in the table.
            assert(table.find(func->getName().str()) != table.end());
        }
        std::vector<llvm::Function *> targetFuncs = {
                func}; // TODO: correctly pick target functions. also the target functions should be in the table.
        patch_function(
                ctx,
                emptyPatch,
                M,
                *func,
                "",
                funcs,
                targetFuncs,
                table
        );
    }

    return llvm::PreservedAnalyses::none();
}

//void Checksum::link_with_hash(llvm::Module &M, std::string &funcName) noexcept {
//    const std::string func = R"(
//@.str = private unnamed_addr constant [6 x i8] c"%02X\0A\00", align 1
//@.str.2 = private unnamed_addr constant [10 x i8] c"hash: %d\0A\00", align 1
//@str = private unnamed_addr constant [6 x i8] c"done!\00", align 1
//
//define i32 @hash5(i64 %0, i32 %1, i32 %2) #0 {
//  %4 = icmp eq i32 %1, 0
//  br i1 %4, label %18, label %5
//
//5:                                                ; preds = %3, %5
//  %6 = phi i32 [ %14, %5 ], [ 0, %3 ]
//  %7 = phi i64 [ %15, %5 ], [ %0, %3 ]
//  %8 = phi i32 [ %16, %5 ], [ %1, %3 ]
//  %9 = inttoptr i64 %7 to i32*
//  %10 = load i32, i32* %9, align 4
//  %11 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([6 x i8], [6 x i8]* @.str, i64 0, i64 0), i32 %10)
//  %12 = load i32, i32* %9, align 4
//  %13 = add i32 %12, %6
//  %14 = mul i32 %13, %2
//  %15 = add i64 %7, 4
//  %16 = add i32 %8, -1
//  %17 = icmp eq i32 %16, 0
//  br i1 %17, label %18, label %5
//
//18:                                               ; preds = %5, %3
//  %19 = phi i32 [ 0, %3 ], [ %14, %5 ]
//  %20 = tail call i32 @puts(i8* nonnull dereferenceable(1) getelementptr inbounds ([6 x i8], [6 x i8]* @str, i64 0, i64 0))
//  %21 = tail call i32 (i8*, ...) @printf(i8* nonnull dereferenceable(1) getelementptr inbounds ([10 x i8], [10 x i8]* @.str.2, i64 0, i64 0), i32 %19)
//  ret i32 %19
//} attributes #0 = {optnone noinline}
//
//declare noundef i32 @printf(i8* nocapture noundef readonly, ...)
//
//declare noundef i32 @puts(i8* nocapture noundef readonly)
//)";
//
//    llvm::SMDiagnostic error;
//    auto funcBuffer = llvm::MemoryBuffer::getMemBuffer(func, "hashFunc", false);
//    auto decodeModule = llvm::parseIR(funcBuffer->getMemBufferRef(), error, M.getContext());
//    if (decodeModule == nullptr) {
//        llvm::errs() << error.getMessage() << '\n';;
//        return;
//    }
//
//    funcName = "hash" + std::to_string(RandomInt64());
//    decodeModule->getFunction("hash5")->setName(funcName);
//
//    llvm::Linker linker(M);
//    if (linker.linkInModule(std::move(decodeModule))) {
//        llvm::errs() << "failed to link modules" << '\n';
//        return;
//    }
//}

void
Checksum::empty_checksum(
        llvm::LLVMContext &ctx,
        llvm::Module &M,
        const std::string &hashFunc,
        llvm::Function *function,
        const std::vector<llvm::Function *> &targetFuncs
) {
    llvm::BasicBlock *entry = &function->getEntryBlock();

    for (auto &targetFunc: targetFuncs) {
        llvm::IRBuilder<> builder(&*entry->getFirstInsertionPt());

        llvm::outs() << "FUNC: " << targetFunc->getName().str() << '\n';

        auto cast = builder.CreatePtrToInt(targetFunc, LLVM_I64(ctx));
//        auto *call = builder.CreateCall(M.getFunction(hashFunc), {
//                cast,
//                LLVM_CONST_I32(ctx, RandomInt8()),
//                LLVM_CONST_I32(ctx, RandomInt8())
//        });
        auto *call = builder.CreateCall(getasm(ctx), {
                // first arg is constant
                LLVM_CONST_I32(ctx, 1),
                // first arg is constant
                LLVM_CONST_I32(ctx, 0),
                // third arg is pionter
                cast,
                // fourth arg is size
                LLVM_CONST_I32(ctx, 2),
        });


        llvm::Value *ExtractedValue = builder.CreateExtractValue(call, {0});


        using namespace llvm;
        // Print the extracted value using printf
        FunctionType *printfType = FunctionType::get(builder.getInt32Ty(), builder.getInt8PtrTy(), true);
        auto printfFunc = M.getOrInsertFunction("printf", printfType);

        Value *formatStr = builder.CreateGlobalStringPtr("%u\n");
        builder.CreateCall(printfFunc, {formatStr, ExtractedValue});

        auto cond = builder.CreateICmpNE(ExtractedValue, LLVM_CONST_I32(ctx, 0));
        auto newBB = llvm::SplitBlockAndInsertIfThen(cond, entry->getTerminator(), false);

        // add exit call for now.
        auto *ftype = llvm::FunctionType::get(
                llvm::Type::getVoidTy(ctx),
                LLVM_I32(ctx),
                false
        );

        auto exitf = M.getOrInsertFunction("exit", ftype);
        builder.SetInsertPoint(newBB);
        builder.CreateCall(exitf, {LLVM_CONST_I32(ctx, CKS_FAIL)});

//        // Inline the crated call.
//        llvm::InlineFunctionInfo ifi;
//        auto inlineop = llvm::InlineFunction(*call, ifi);
//        if (!inlineop.isSuccess()) {
//            llvm::outs() << inlineop.getFailureReason() << '\n';
//            throw std::runtime_error("failed to inline hash function");
//        }

        llvm::outs() << "FUNC: " << targetFunc->getName().str() << " DONE" << '\n';
    }
}

void
Checksum::patch_function(
        llvm::LLVMContext &ctx,
        bool emptyPatch,
        llvm::Module &M,
        llvm::Function &F,
        const std::string &hashFunc,
        const std::vector<llvm::Function *> &allFuncs,
        const std::vector<llvm::Function *> &targetFuncs,
        const std::unordered_map<std::string, Function> &allFuncsMetadata
) {
    llvm::IRBuilder<> builder(&*F.getEntryBlock().getFirstInsertionPt());

    if (emptyPatch) {
        empty_checksum(ctx, M, hashFunc, &F, targetFuncs);
    } else {
        final_checksum(ctx, M, hashFunc, &F, targetFuncs, allFuncsMetadata);
    }

    llvm::BasicBlock *SplitBlock = &*RandomElement(F.begin(), F.end());
    while (SplitBlock->isLandingPad()) {
        SplitBlock = &*RandomElement(F.begin(), F.end());
    }
    llvm::Instruction *SplitInstruction = RandomNonPHIInstruction(*SplitBlock);
    llvm::Value *RandomFunc = *RandomElement(allFuncs.begin(), allFuncs.end());

    builder.SetInsertPoint(SplitInstruction);
    llvm::Value *RandomInteger = builder.CreatePtrToInt(RandomFunc, LLVM_I64(ctx));

    auto *predicate = OpaquePredicates::getRandomOpaquelyTruePredicate();
    llvm::Value *condition = (&*predicate)(RandomInteger, SplitInstruction);
    auto newBB = llvm::SplitBlockAndInsertIfThen(condition, SplitInstruction, false);

    // since we're using a true predicate swap the branch instruction successor.
    llvm::cast<llvm::BranchInst>(SplitBlock->getTerminator())->swapSuccessors();

    std::string a;
    if (emptyPatch || SkipCalculatingParity.getValue()) {
        a = PARITY_INSTRUCTION;
    } else {
        a = calculate_parity(&F, allFuncsMetadata.at(F.getName().str()));
    }

    llvm::InlineAsm *parity = llvm::InlineAsm::get(
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
            a,
            "",
            true
    );

    builder.SetInsertPoint(newBB);
    builder.CreateCall(parity, {});
}

void
Checksum::write_func_requests(
        const std::string &outFile,
        const std::vector<std::string> &funcs
) noexcept {
    // Create an odd number
    std::vector<HashRequest> requests;

    for (auto &f: funcs) {
        uint64_t odd = RandomInt64(0, (std::numeric_limits<uint8_t>::max() / 2) % 21);
        odd = odd * 2 + 1;
        requests.push_back({odd, f});
    }

    Input input = {requests};
    nlohmann::json json = input;

    std::ofstream outputFile(outFile);
    if (outputFile.is_open()) {
        outputFile << json.dump(4);
        outputFile.close();
        llvm::outs() << "JSON written to file successfully." << '\n';
    } else {
        llvm::errs() << "Unable to open file for writing." << '\n';
    }
}

std::unordered_map<std::string, Function>
Checksum::read_func_metadata(const std::string &infile) {
    std::unordered_map<std::string, Function> table;

    if (infile.empty()) {
        return table;
    }

    std::ifstream inputFile(infile);
    if (!inputFile.is_open()) {
        llvm::errs() << "Could not open the file." << "\n";
        throw std::runtime_error("failed to open file");
    }

    nlohmann::json j;
    inputFile >> j;

    std::vector<Function> functions = j.get<std::vector<Function>>();

    for (auto &f: functions) {
        table[f.function] = f;
    }

    return table;
}

void
Checksum::final_checksum(
        llvm::LLVMContext &ctx,
        llvm::Module &M,
        const std::string &hashFunc,
        llvm::Function *function,
        const std::vector<llvm::Function *> &targetFuncs,
        const std::unordered_map<std::string, Function> &allFuncsMetadata
) {
    llvm::BasicBlock *entry = &function->getEntryBlock();

    for (auto &targetFunc: targetFuncs) {
        llvm::IRBuilder<> builder(&*entry->getFirstInsertionPt());

        llvm::outs() << "FUNC: " << targetFunc->getName().str() << '\n';

        auto metadata = allFuncsMetadata.at(function->getName().str());
        auto cast = builder.CreatePtrToInt(targetFunc, LLVM_I64(ctx));
//        auto *call = builder.CreateCall(M.getFunction(hashFunc), {
//                cast,
//                LLVM_CONST_I32(ctx, metadata.instruction_count),
//                LLVM_CONST_I32(ctx, metadata.constant)
//        });

        auto *call = builder.CreateCall(getasm(ctx), {
                // first arg is constant
                LLVM_CONST_I32(ctx, metadata.constant),
                // first arg is constant
                LLVM_CONST_I32(ctx, 0),
                // third arg is pionter
                cast,
                // fourth arg is size
                LLVM_CONST_I32(ctx, metadata.instruction_count)
        });


        llvm::Value *ExtractedValue = builder.CreateExtractValue(call, {0});


        using namespace llvm;
        // Print the extracted value using printf
        FunctionType *printfType = FunctionType::get(builder.getInt32Ty(), builder.getInt8PtrTy(), true);
        auto printfFunc = M.getOrInsertFunction("printf", printfType);

        Value *formatStr = builder.CreateGlobalStringPtr("%u\n");
        builder.CreateCall(printfFunc, {formatStr, ExtractedValue});

        auto cond = builder.CreateICmpNE(ExtractedValue, LLVM_CONST_I32(ctx, 0));
        auto newBB = llvm::SplitBlockAndInsertIfThen(cond, entry->getTerminator(), false);

        // add exit call for now.
        auto *ftype = llvm::FunctionType::get(
                llvm::Type::getVoidTy(ctx),
                LLVM_I32(ctx),
                false
        );

        auto exitf = M.getOrInsertFunction("exit", ftype);
        builder.SetInsertPoint(newBB);
        builder.CreateCall(exitf, {LLVM_CONST_I32(ctx, CKS_FAIL)});

//        // Inline the crated call.
//        llvm::InlineFunctionInfo ifi;
//        auto inlineop = llvm::InlineFunction(*call, ifi);
//        if (!inlineop.isSuccess()) {
//            llvm::outs() << inlineop.getFailureReason() << '\n';
//            throw std::runtime_error("failed to inline hash function");
//        }

        llvm::outs() << "FUNC: " << targetFunc->getName().str() << " DONE" << '\n';
    }
}

std::string
Checksum::calculate_parity(
        llvm::Function *F,
        const Function &FuncMetadata
) {
    size_t parity_count = 0;
    for (uint32_t i: FuncMetadata.instructions) {
        if (i == PARITY_INSTRUCTION_INT) {
            parity_count++;
        }
    }

    assert(parity_count == 1); // if we encounter multiple we need to chose a different value.

    std::vector<uint32_t> copy(FuncMetadata.instructions.begin(), FuncMetadata.instructions.end());

    uint64_t z = 0;
    uint64_t c = FuncMetadata.constant;
    int64_t parity_c = 0;
    auto idx = copy.rend();

    for (auto beg = copy.rbegin(); beg != copy.rend(); ++beg) {
        uint32_t tmp = *beg;
        *beg = *beg * c;

        if (tmp != PARITY_INSTRUCTION_INT) {
            z += *beg;
        } else {
            parity_c = c;
            idx = beg;
        }

        c = c * FuncMetadata.constant;
    }

    int64_t x, y;
    eegcd(parity_c, x, y);
    x = x * ((int64_t(std::numeric_limits<uint32_t>::max()) + 1) - z);
    x = x % (int64_t(std::numeric_limits<uint32_t>::max()) + 1);

    *idx = x * parity_count;

    auto result = uint32_t(x * parity_count);

    // check if the instructions sum to 0
    uint32_t sum = 0;
    for (uint32_t i: copy) {
        sum += i;
    }

    assert(sum == 0);

    std::string out = ".byte ";
    {
        std::stringstream stream;
        stream << std::hex << ((result >> 24) & 0xff);
        out += "0x" + stream.str() + ", ";
    }
    {
        std::stringstream stream;
        stream << std::hex << ((result >> 16) & 0xff);
        out += "0x" + stream.str() + ", ";
    }
    {
        std::stringstream stream;
        stream << std::hex << ((result >> 8) & 0xff);
        out += "0x" + stream.str() + ", ";
    }
    {
        std::stringstream stream;
        stream << std::hex << ((result) & 0xff);
        out += "0x" + stream.str();
    }


    return out;
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
