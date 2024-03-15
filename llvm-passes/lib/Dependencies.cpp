#include "PufPatcher.h"

void PufPatcher::init_deps(llvm::Module &M) {
    auto &ctx = M.getContext();

    lib_c_dependencies.pthread_attr_init_func = M.getOrInsertFunction(
            "pthread_attr_init",
            llvm::FunctionType::get(
                    LLVM_I32(ctx),
                    {llvm::PointerType::getInt8PtrTy(ctx)},
                    false
            )
    );

    lib_c_dependencies.pthread_attr_setdetachstate_func = M.getOrInsertFunction(
            "pthread_attr_setdetachstate",
            llvm::FunctionType::get(
                    LLVM_I32(ctx),
                    {llvm::PointerType::getInt8PtrTy(ctx), LLVM_I32(ctx)},
                    false
            )
    );

    lib_c_dependencies.pthread_create_func = M.getOrInsertFunction(
            "pthread_create",
            llvm::FunctionType::get(
                    LLVM_I32(ctx),
                    {
                            llvm::PointerType::getInt8PtrTy(ctx),
                            llvm::PointerType::getInt8PtrTy(ctx),
                            llvm::PointerType::getInt8PtrTy(ctx),
                            llvm::PointerType::getInt8PtrTy(ctx)
                    },
                    false
            )
    );

    global_variables.stdoutput = M.getGlobalVariable("stdout");
    if (!global_variables.stdoutput) {
        global_variables.stdoutput = new llvm::GlobalVariable(
                M,
                llvm::PointerType::getInt8PtrTy(ctx),
                false,
                llvm::GlobalValue::ExternalLinkage,
                nullptr,
                "stdout",
                nullptr,
                llvm::GlobalValue::NotThreadLocal,
                std::nullopt,
                true
        );
    }

    // bring printf, fflush and sleep into scope.
    lib_c_dependencies.printf_arg_type = llvm::PointerType::getUnqual(LLVM_I8(ctx));

    lib_c_dependencies.printf_func = M.getOrInsertFunction(
            "printf",
            llvm::FunctionType::get(
                    LLVM_I32(ctx),
                    lib_c_dependencies.printf_arg_type,
                    true
            )
    );

    lib_c_dependencies.sleep_func = M.getOrInsertFunction(
            "sleep",
            llvm::FunctionType::get(
                    LLVM_I32(ctx),
                    {LLVM_I32(ctx)},
                    false
            )
    );

    lib_c_dependencies.fflush_func = M.getOrInsertFunction(
            "fflush",
            llvm::FunctionType::get(
                    LLVM_I32(ctx),
                    {llvm::PointerType::getInt8PtrTy(ctx)},
                    false
            )
    );

    lib_c_dependencies.close_func = M.getOrInsertFunction(
            "close",
            llvm::FunctionType::get(
                    LLVM_I32(ctx),
                    {LLVM_I32(ctx)},
                    false
            )
    );

    lib_c_dependencies.write_func = M.getOrInsertFunction(
            "write",
            llvm::FunctionType::get(
                    LLVM_I32(ctx),
                    {LLVM_I32(ctx), llvm::Type::getInt8PtrTy(ctx), LLVM_I32(ctx)},
                    false
            )
    );

    lib_c_dependencies.read_func = M.getOrInsertFunction(
            "read",
            llvm::FunctionType::get(
                    LLVM_I32(ctx),
                    {LLVM_I32(ctx), llvm::Type::getInt8PtrTy(ctx), LLVM_I32(ctx)},
                    false
            )
    );

    lib_c_dependencies.exit_func = M.getOrInsertFunction(
            "exit",
            llvm::FunctionType::get(
                    llvm::Type::getVoidTy(ctx),
                    {LLVM_I32(ctx)},
                    false)
    );

    // Create global variable for the file descriptor
    global_variables.puf_fd = M.getGlobalVariable("____puf_fd____");
    if (!global_variables.puf_fd) {
        global_variables.puf_fd = new llvm::GlobalVariable(
                M,
                LLVM_I32(ctx),
                false,
                llvm::GlobalValue::InternalLinkage,
                LLVM_CONST_I32(ctx, 0), "____puf_fd____"
        );
    }

    lib_c_dependencies.open_func = M.getOrInsertFunction("open", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {
                    llvm::Type::getInt8PtrTy(ctx),
                    LLVM_I32(ctx)
            },
            false
    ));
}

std::pair<llvm::GlobalVariable *, size_t>
PufPatcher::create_puf_array(llvm::Module &M, crossover::EnrollData &enrollment) {
    assert(M.getGlobalVariable("____puf_array____") == nullptr);

    auto &ctx = M.getContext();
    auto puf_array_typ = llvm::ArrayType::get(
            LLVM_U32(ctx),
            enrollment.requests.size()
    );

    std::vector<llvm::Constant *> puf_array_data;
    puf_array_data.resize(enrollment.requests.size());

    for (auto &i: puf_array_data) {
        i = LLVM_CONST_I32(ctx, 0);
    }

    auto *puf_array = new llvm::GlobalVariable{
            M,
            puf_array_typ,
            false,
            llvm::GlobalValue::InternalLinkage,
            llvm::ConstantArray::get(puf_array_typ, puf_array_data),
            "____puf_array____"
    };

    return {puf_array, enrollment.requests.size()};
}