#include <sys/fcntl.h>

#include "PufPatcher.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

llvm::Function *
PufPatcher::puf_open_ctor(
        llvm::Module &M,
        const crossover::EnrollData &enrollments,
        llvm::GlobalVariable *const Fd
) {
    auto &ctx = M.getContext();

    auto *puf_func = llvm::Function::Create(
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
            llvm::Function::InternalLinkage,
            "____open_device____",
            &M
    );

    llvm::IRBuilder<> Builder(llvm::BasicBlock::Create(ctx, "entry", puf_func));
    auto *fd = Builder.CreateCall(lib_c_dependencies.open_func, {
            Builder.CreateGlobalStringPtr("/dev/puf"), Builder.getInt32(O_RDWR)
    });

    // error handle the file descriptor.
    auto *is_not_open_fd = Builder.CreateICmpSLT(fd, Builder.getInt32(0));
    auto *failed_bb = llvm::BasicBlock::Create(ctx, "error", puf_func);
    auto *success_bb = llvm::BasicBlock::Create(ctx, "continue", puf_func);

    Builder.CreateCondBr(is_not_open_fd, failed_bb, success_bb);

    // Handle the error case first, by exiting the program.
    {
        Builder.SetInsertPoint(failed_bb);
        Builder.CreateCall(lib_c_dependencies.exit_func, {Builder.getInt32(DEV_FAIL)});
        Builder.CreateUnreachable();
    }

    // handle ok path.
    Builder.SetInsertPoint(success_bb);
    Builder.CreateStore(fd, Fd);

    // Parity Bits + Cell Pointers
    size_t array_length_bytes = 0;
    for (auto &enrollment: enrollments.enrollments) {
        // | parity length| parity bits| pointers bits|
        array_length_bytes += 1;
        array_length_bytes += enrollment.parity.size() * sizeof(uint16_t);
        array_length_bytes += enrollment.pointers.size() * sizeof(uint32_t);
        assert(enrollment.pointers.size() == 32); // should encode 32bits.
    }

    std::vector<uint8_t> enrollment_data;
    enrollment_data.resize(array_length_bytes);
    uint32_t write_idx = 0;

    for (auto &enrollment: enrollments.enrollments) {
        fill_8bits(&write_idx, &enrollment_data[0], uint8_t(enrollment.parity.size()));
        for (auto parity: enrollment.parity) {
            fill_16bits(&write_idx, &enrollment_data[0], parity);
        }
        for (auto ptr: enrollment.pointers) {
            fill_32bits(&write_idx, &enrollment_data[0], ptr);
        }
    }

    // Create Array with enrollment data.
    std::vector<llvm::Constant *> llvm_arr_values;
    llvm_arr_values.reserve(enrollment_data.size());

    for (uint8_t val: enrollment_data) {
        llvm_arr_values.push_back(llvm::ConstantInt::get(LLVM_I8(ctx), val));
    }

    auto *arr_type = llvm::ArrayType::get(LLVM_I8(ctx), array_length_bytes);
    llvm::Constant *llvm_enrollment_data = llvm::ConstantArray::get(arr_type, llvm_arr_values);

    // Write to PUF
    auto llvm_arr = Builder.CreateAlloca(arr_type, nullptr, "enrollment_data");
    Builder.CreateStore(llvm_enrollment_data, llvm_arr);
    auto *llvm_arr_ptr = Builder.CreateBitCast(llvm_arr, llvm::PointerType::get(arr_type, 0));
    Builder.CreateCall(lib_c_dependencies.write_func, {fd, llvm_arr_ptr, LLVM_CONST_I32(ctx, array_length_bytes)});

    Builder.CreateRetVoid();

    llvm::appendToGlobalCtors(M, puf_func, std::numeric_limits<int>::min());

    return puf_func;
}

llvm::Function *PufPatcher::puf_close_dtor(llvm::Module &M, llvm::GlobalVariable *const Fd) {
    auto &ctx = M.getContext();
    auto *puf_func = llvm::Function::Create(
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
            llvm::Function::InternalLinkage,
            "____close_device____",
            &M
    );

    auto *BB = llvm::BasicBlock::Create(ctx, "entry", puf_func);
    llvm::IRBuilder<> Builder(BB);
    auto *fd = Builder.CreateLoad(Fd->getValueType(), Fd);
    Builder.CreateCall(lib_c_dependencies.close_func, {fd});
    Builder.CreateRetVoid();

    llvm::appendToGlobalDtors(M, puf_func, 0);
    return puf_func;
}

void PufPatcher::spawn_puf_thread(
        llvm::Module &M,
        const std::pair<llvm::GlobalVariable *, size_t> &puf_array,
        llvm::Function *const function_to_add_code,
        const crossover::EnrollData &enrollments
) {
    auto &ctx = M.getContext();

    auto thread_function = llvm::Function::Create(
            llvm::FunctionType::get(
                    llvm::PointerType::getInt8PtrTy(ctx),
                    {llvm::PointerType::getInt8PtrTy(ctx)},
                    false
            ),
            llvm::Function::InternalLinkage,
            "____puf_reader____",
            M
    );
    thread_function->addFnAttr(llvm::Attribute::NoInline);

    llvm::IRBuilder<> Builder(llvm::BasicBlock::Create(thread_function->getContext(), "entry", thread_function));

    auto &[puf_array_ptr, _] = puf_array;

    auto *printf_format_str = Builder.CreateGlobalStringPtr("PUF response: 0x%08x\n");
    auto *format_str_ptr = Builder.CreatePointerCast(printf_format_str, lib_c_dependencies.printf_arg_type,
                                                     "formatStr");

    auto *puf_response_ptr = Builder.CreateAlloca(LLVM_U32(ctx));
    Builder.CreateStore(LLVM_CONST_I32(ctx, 0), puf_response_ptr);

    uint32_t last_sleep = 0x0;
    for (int i = 0; i < enrollments.requests.size(); i++) {
        uint32_t sleep_for = enrollments.requests[i] - last_sleep;
        last_sleep = enrollments.requests[i];
        Builder.CreateCall(lib_c_dependencies.sleep_func, {LLVM_CONST_I32(ctx, sleep_for + enrollments.read_with_delay)});
        Builder.CreateCall(lib_c_dependencies.read_func,
                           {
                                   Builder.CreateLoad(LLVM_I32(ctx), global_variables.puf_fd),
                                   puf_response_ptr,
                                   LLVM_CONST_I32(ctx, sizeof(uint32_t))
                           });

        auto puf_array_offset_ptr = Builder.CreateInBoundsGEP(
                puf_array_ptr->getValueType(),
                puf_array_ptr,
                {
                        LLVM_CONST_I32(ctx, 0),
                        LLVM_CONST_I32(ctx, i)
                }
        );

        Builder.CreateCall(
                lib_c_dependencies.printf_func,
                {format_str_ptr, Builder.CreateLoad(LLVM_U32(ctx), puf_response_ptr)}
        );
        Builder.CreateCall(
                lib_c_dependencies.fflush_func,
                {Builder.CreateLoad(global_variables.stdoutput->getValueType(), global_variables.stdoutput)}
        );
        Builder.CreateStore(
                Builder.CreateAdd(
                        Builder.CreateLoad(LLVM_U32(ctx), puf_response_ptr),
                        Builder.CreateLoad(LLVM_U32(ctx), puf_array_offset_ptr)
                ),
                puf_array_offset_ptr
        );
    }

    Builder.CreateRet(llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(ctx)));

    // Add code to spawn detached thread for the above created function.
    {
        Builder.SetInsertPoint(&*function_to_add_code->getEntryBlock().getFirstInsertionPt());

        // Create a struct type with two members: an i32 and an array of 32 i8 values
        auto *union_pthread_attr_t = llvm::StructType::create(
                ctx,
                {LLVM_I32(ctx), llvm::ArrayType::get(LLVM_I8(ctx), 32)},
                "union.pthread_attr_t"
        );

        auto *thread_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
        auto *union_ptr = Builder.CreateAlloca(union_pthread_attr_t);

        Builder.CreateCall(lib_c_dependencies.pthread_attr_init_func, {union_ptr});
        Builder.CreateCall(lib_c_dependencies.pthread_attr_setdetachstate_func, {union_ptr, LLVM_CONST_I32(ctx, 1)});
        Builder.CreateCall(lib_c_dependencies.pthread_create_func, {
                thread_ptr,
                union_ptr,
                thread_function,
                llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(ctx))
        });
    }
}
