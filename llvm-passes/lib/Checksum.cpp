#include <sstream>

#include "Checksum.h"
#include "OpaquePredicates.h"

#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

static llvm::cl::opt<bool> SkipCalculatingParity(
        "pskip",
        llvm::cl::desc(
                "when enabled the LLVM pass skips replacing the PARITY placeholder. Useful for mutli-run patch of a binary."),
        llvm::cl::value_desc("bool"),
        llvm::cl::Optional
);

void Checksum::run(llvm::Module &M, std::vector<llvm::Function *> funcs) {
    // Table contains the functions which can be actually checksummed
    std::unordered_map<std::string, crossover::Function> table;

    // If the inputfile is empty this will be a empty patch i.e the patch
    // will not contain actual values, just the instructions.
    bool emptyPatch = table.empty();
    if (emptyPatch) {
        llvm::outs() << "InputFile empty: patching skeleton only" << '\n';
    }

    auto &ctx = M.getContext();
    for (auto &func: funcs) {
        if (!emptyPatch) { // each function we are going to patch should lbe in the table.
            assert(table.find(func->getName().str()) != table.end());
        }
        std::vector<llvm::Function *> targetFuncs = {
                func
        }; // TODO: correctly pick target functions. also the target functions should be in the table.

        patch_function(
                ctx,
                emptyPatch,
                M,
                *func,
                funcs,
                targetFuncs,
                table
        );
    }
}

void
Checksum::patch_function(
        llvm::LLVMContext &ctx,
        bool emptyPatch,
        llvm::Module &M,
        llvm::Function &F,
        const std::vector<llvm::Function *> &allFuncs,
        const std::vector<llvm::Function *> &targetFuncs,
        const std::unordered_map<std::string, crossover::Function> &allFuncMetadata
) noexcept {
    uint32_t seed = std::accumulate(F.getName().begin(), F.getName().end(), 0);
    auto rng = RandomRNG(seed);

    llvm::IRBuilder<> builder(&*F.getEntryBlock().getFirstInsertionPt());

    add_checksum(ctx, emptyPatch, M, &F, targetFuncs, allFuncMetadata);

    llvm::BasicBlock *SplitBlock = *exitBlocks(F).begin();
    llvm::Instruction *SplitInstruction = SplitBlock->getTerminator();

    llvm::Value *RandomFunc = *RandomElementRNG(allFuncs.begin(), allFuncs.end(), rng);

    builder.SetInsertPoint(SplitInstruction);
    llvm::Value *RandomInteger = builder.CreatePtrToInt(RandomFunc, LLVM_I64(ctx));

    auto *predicate = OpaquePredicates::getRandomOpaquelyTruePredicate(rng);
    llvm::Value *condition = (&*predicate)(RandomInteger, SplitInstruction);
    auto newBB = llvm::SplitBlockAndInsertIfThen(condition, SplitInstruction, false);

    // since we're using a true predicate swap the branch instruction successor.
    llvm::cast<llvm::BranchInst>(SplitBlock->getTerminator())->swapSuccessors();

    std::string a;
    {
        if (emptyPatch || SkipCalculatingParity.getValue()) {
            a = PARITY_INSTRUCTION;
        } else {
            a = calculate_parity(allFuncMetadata.at(F.getName().str()));
        }
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
Checksum::add_checksum(
        llvm::LLVMContext &ctx,
        bool emptyPatch,
        llvm::Module &M,
        llvm::Function *function,
        const std::vector<llvm::Function *> &targetFuncs,
        const std::unordered_map<std::string, crossover::Function> &allFuncsMetadata
) noexcept {
    llvm::BasicBlock *entry = &function->getEntryBlock();

    for (auto &targetFunc: targetFuncs) {
        llvm::IRBuilder<> builder(&*entry->getFirstInsertionPt());

        llvm::Value *call = nullptr;
        {
            if (!emptyPatch) {
                auto metadata = allFuncsMetadata.at(function->getName().str());
                auto cast = builder.CreatePtrToInt(targetFunc, LLVM_I64(ctx));
                call = builder.CreateCall(checksum(ctx), {
                        LLVM_CONST_I32(ctx, metadata.constant),
                        LLVM_CONST_I32(ctx, 0), // accumulator
                        cast,
                        LLVM_CONST_I32(ctx, metadata.instruction_count)
                });
            } else {
                auto cast = builder.CreatePtrToInt(targetFunc, LLVM_I64(ctx));
                call = builder.CreateCall(checksum(ctx), {
                        LLVM_CONST_I32(ctx, 1),
                        LLVM_CONST_I32(ctx, 0), // accumulator
                        cast,
                        LLVM_CONST_I32(ctx, 2),
                });
            }
        }

        auto *checksum = builder.CreateExtractValue(call, {0});
        auto cond = builder.CreateICmpNE(checksum, LLVM_CONST_I32(ctx, 0));
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
    }
}

std::string Checksum::calculate_parity(const crossover::Function &FuncMetadata) noexcept {
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
    uint64_t parity_c = 0;
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

    uint64_t x, y;
    eegcd(parity_c, x, y);
    x = x * ((int64_t(std::numeric_limits<uint32_t>::max()) + 1) - z);
    x = x % (int64_t(std::numeric_limits<uint32_t>::max()) + 1);

    *idx = x * parity_c;

    auto result = uint32_t(x);

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

llvm::InlineAsm *Checksum::checksum(llvm::LLVMContext &ctx) noexcept {
    auto typ = llvm::FunctionType::get(
            llvm::StructType::get(LLVM_I32(ctx), LLVM_I64(ctx), LLVM_I32(ctx)),
            {LLVM_I32(ctx), LLVM_I32(ctx), LLVM_I64(ctx), LLVM_I32(ctx)},
            false
    );

    std::string checksum = R"(1:
ldr r3, [$1], #4
rev r3, r3
mul $0, $3, $0
add $0, $0, r3
subs $2, $2, #1
bne 1b
)";

    return llvm::InlineAsm::get(
            typ,
            checksum,
            "=r,=r,=r,r,0,1,2,~{r3},~{cc},~{memory}",
            true
    );
}