#include "PufPatcher.h"

#include <fstream>
#include <sys/fcntl.h>

#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

static llvm::cl::opt<std::string> EnrollmentFile(
        "enrollment",
        llvm::cl::desc("Enrollment json file that was generated"),
        llvm::cl::value_desc("string"),
        llvm::cl::Required
);

llvm::PreservedAnalyses PufPatcher::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
    init_deps(M);

    enrollment::EnrollData enrollments = read_enrollment_data();

    // open the /dev/puf in a ctor
    // It will open the device, write the enrollment data to it.
    auto ctor = puf_open_ctor(enrollments, M, puf_fd);

    // close /dev/puf in a dtor
    puf_close_dtor(M, puf_fd);

    std::vector<llvm::Function *> functions_to_patch;
    for (auto &f: M) {
        if (f.isIntrinsic() || f.isDeclaration() || f.empty()) {
            continue;
        }
        functions_to_patch.push_back(&f);
    }

    // Analyse call graph before replacing with indirect calls and before adding
    // PUF thread.
    auto call_graph = llvm::CallGraphAnalysis().run(M, AM);

    // Replaces all calls/invokes in the collected functions and creates a lookup table
    // where each function has it place which will be then computed when receiving the correct PUF response.
    // After this function only the call_graph should be used for identifying the calls.
    auto [lookup_table, call_mappings] = replace_calls_with_lookup_table(M, functions_to_patch);

    // Identifies all possibly external entry points and determines at which places
    // the functions addresses in the lookup table should be computed.
    insert_address_calculations(enrollments, lookup_table, call_mappings, call_graph);

    // add code that spawns a detached thread that will perform the PUF readings.
    // Function call added will not be in the lookup table created above, this is by design.
    spawn_puf_thread(ctor, M, enrollments);

    // add checksums of the patched functions.
//    checksum.run(M, funcs);

    return llvm::PreservedAnalyses::none();
}

llvm::Function *PufPatcher::puf_open_ctor(
        enrollment::EnrollData &enrollments,
        llvm::Module &M,
        llvm::GlobalVariable *Fd
) {
    auto &ctx = M.getContext();

    auto *puf_func = llvm::Function::Create(
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false),
            llvm::Function::InternalLinkage,
            "____open_device____",
            &M
    );

    llvm::IRBuilder<> Builder(llvm::BasicBlock::Create(ctx, "entry", puf_func));
    auto *fd = Builder.CreateCall(open_func, {
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
        // TODO: remove this to correcly exit.
        // Only for testing.
//        Builder.CreateBr(success_bb);

        Builder.CreateCall(exit_func, {Builder.getInt32(DEV_FAIL)});
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
    Builder.CreateCall(write_func, {fd, llvm_arr_ptr, LLVM_CONST_I32(ctx, array_length_bytes)});

    Builder.CreateRetVoid();

    appendToGlobalCtors(M, puf_func, std::numeric_limits<int>::min());

    return puf_func;
}

llvm::Function *PufPatcher::puf_close_dtor(llvm::Module &M, llvm::GlobalVariable *Fd) {
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
    Builder.CreateCall(close_func, {fd});
    Builder.CreateRetVoid();

    appendToGlobalDtors(M, puf_func, 0);
    return puf_func;
}

enrollment::EnrollData PufPatcher::read_enrollment_data() {
    llvm::outs() << "input enrollment file: " << EnrollmentFile << "\n";
    std::ifstream inputFile(EnrollmentFile);
    if (!inputFile.is_open()) {
        llvm::errs() << "Could not open the file." << "\n";
        throw std::runtime_error("failed to open file");
    }

    nlohmann::json j;
    inputFile >> j;

    return j.get<enrollment::EnrollData>();
}

std::pair<
        llvm::GlobalVariable *,
        std::map<llvm::Function *, uint32_t>
>
PufPatcher::replace_calls_with_lookup_table(
        llvm::Module &M,
        std::vector<llvm::Function *> &funcs
) {
    std::map<std::string, std::vector<llvm::CallBase *>> mappings;

    // for each function collect all call instructions.
    for (auto f: funcs) {
        assert(!f->getName().str().empty());
        assert(mappings.find(f->getName().str()) == mappings.end());

        mappings[f->getName().str()] = {};

        for (auto &bb: *f) {
            for (auto &i: bb) {
                if (auto *is_call = llvm::dyn_cast<llvm::CallBase>(&i); is_call) {
                    if (auto calle = is_call->getCalledFunction(); calle) { // ignore already indirect calls.
                        if (calle->isIntrinsic()) {
                            continue;
                        }
                        mappings[f->getName().str()].push_back(is_call);
                    }
                }
            }
        }
    }

    // TODO: replace this with nullptrs.

    // group calls to replace and their occurrences.
    std::map<llvm::Function *, std::vector<llvm::CallBase *>> group_calls;
    for (auto &[_, calls]: mappings) {
        for (auto &call: calls) {
            group_calls[call->getCalledFunction()].push_back(call);
        }
    }

    // create a global lookup table of functions addresses.
    size_t lookup_table_size = group_calls.size();

    std::vector<llvm::Constant *> lookup_table_data;
    lookup_table_data.reserve(lookup_table_size);

    auto &ctx = M.getContext();
    for (auto &[func, _]: group_calls) {
        // create pointer to function.
        lookup_table_data.push_back(
                llvm::ConstantExpr::getPtrToInt(M.getFunction(func->getName().str()), LLVM_I32(ctx))
        );
    }

    auto lookup_table_typ = llvm::ArrayType::get(
            LLVM_I32(ctx),
            lookup_table_size
    );

    auto lookup_table = new llvm::GlobalVariable(
            M,
            lookup_table_typ,
            false,
            llvm::GlobalValue::InternalLinkage,
            llvm::ConstantArray::get(lookup_table_typ, lookup_table_data),
            "lookup_table"
    );

    // replace all occurrences with an indirect call via the table.
    uint32_t idx = 0;
    std::map<llvm::Function *, uint32_t> func_to_lookup_idx;
    for (auto &[func, calls]: group_calls) {
        func_to_lookup_idx[func] = idx;
        for (auto &call: calls) {
            llvm::IRBuilder<> Builder(call);

            auto index = Builder.CreateAlloca(LLVM_I32(ctx));
            Builder.CreateStore(LLVM_CONST_I32(ctx, idx), index, true);

            auto ptr_to_table = Builder.CreateInBoundsGEP(
                    lookup_table->getValueType(),
                    lookup_table,
                    {
                            LLVM_CONST_I32(ctx, 0),
                            Builder.CreateLoad(LLVM_I32(ctx), index)
                    }
            );

            auto load = Builder.CreateLoad(
                    llvm::PointerType::getInt8PtrTy(ctx),
                    ptr_to_table
            );

            if (auto call_inst = llvm::dyn_cast<llvm::CallInst>(call); call_inst) {
                auto *new_call = Builder.CreateCall(
                        call_inst->getCalledFunction()->getFunctionType(),
                        Builder.CreateBitCast(load, call_inst->getCalledFunction()->getType()),
                        std::vector<llvm::Value *>(call_inst->args().begin(), call_inst->args().end()),
                        ""
                );
                llvm::ReplaceInstWithInst(call, new_call);
            }
            if (auto invoke_inst = llvm::dyn_cast<llvm::InvokeInst>(call); invoke_inst) {
                auto *new_call = Builder.CreateInvoke(
                        invoke_inst->getCalledFunction()->getFunctionType(),
                        Builder.CreateBitCast(load, invoke_inst->getCalledFunction()->getType()),
                        invoke_inst->getNormalDest(),
                        invoke_inst->getUnwindDest(),
                        std::vector<llvm::Value *>(invoke_inst->args().begin(), invoke_inst->args().end()),
                        ""
                );
                llvm::ReplaceInstWithInst(call, new_call);
            }
        }

        idx++;
    }

    return std::make_pair(lookup_table, std::move(func_to_lookup_idx));
}

void PufPatcher::spawn_puf_thread(
        llvm::Function *function_to_add_code,
        llvm::Module &M,
        enrollment::EnrollData &enrollments
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

    // TODO: Fixup
    auto *printf_format_str = Builder.CreateGlobalStringPtr("PUF response: 0x%08x\n");
    auto *format_str_ptr = Builder.CreatePointerCast(printf_format_str, printf_arg_type, "formatStr");
    auto *puf_response = Builder.CreateAlloca(LLVM_U32(ctx));
    Builder.CreateStore(LLVM_CONST_I32(ctx, 0), puf_response);
    auto *local_puf_fd = Builder.CreateLoad(LLVM_I32(ctx), puf_fd);
    for (uint32_t timeout: enrollments.requests) {
        Builder.CreateCall(sleep_func, {LLVM_CONST_I32(ctx, timeout)});
        Builder.CreateCall(read_func,
                           {
                                   local_puf_fd,
                                   puf_response,
                                   LLVM_CONST_I32(ctx, sizeof(uint32_t))
                           });
        Builder.CreateCall(printf_func, {format_str_ptr, Builder.CreateLoad(LLVM_U32(ctx), puf_response)});
        Builder.CreateCall(fflush_func, {Builder.CreateLoad(stdoutput->getValueType(), stdoutput)});
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

        Builder.CreateCall(pthread_attr_init_func, {union_ptr});
        Builder.CreateCall(pthread_attr_setdetachstate_func, {union_ptr, LLVM_CONST_I32(ctx, 1)});
        Builder.CreateCall(pthread_create_func, {
                thread_ptr,
                union_ptr,
                thread_function,
                llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(ctx))
        });
    }
}

void PufPatcher::init_deps(llvm::Module &M) {
    auto &ctx = M.getContext();

    pthread_attr_init_func = M.getOrInsertFunction("pthread_attr_init", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {llvm::PointerType::getInt8PtrTy(ctx)},
            false
    ));

    pthread_attr_setdetachstate_func = M.getOrInsertFunction("pthread_attr_setdetachstate", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {llvm::PointerType::getInt8PtrTy(ctx), LLVM_I32(ctx)},
            false
    ));

    pthread_create_func = M.getOrInsertFunction("pthread_create", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {
                    llvm::PointerType::getInt8PtrTy(ctx),
                    llvm::PointerType::getInt8PtrTy(ctx),
                    llvm::PointerType::getInt8PtrTy(ctx),
                    llvm::PointerType::getInt8PtrTy(ctx)
            },
            false
    ));

    stdoutput = M.getGlobalVariable("stdout");
    if (!stdoutput) {
        stdoutput = new llvm::GlobalVariable(M,
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
    printf_arg_type = llvm::PointerType::getUnqual(LLVM_I8(ctx));

    printf_func = M.getOrInsertFunction("printf", llvm::FunctionType::get(
            LLVM_I32(ctx),
            printf_arg_type,
            true
    ));

    sleep_func = M.getOrInsertFunction("sleep", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {LLVM_I32(ctx)},
            false
    ));

    fflush_func = M.getOrInsertFunction("fflush", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {llvm::PointerType::getInt8PtrTy(ctx)},
            false
    ));

    close_func = M.getOrInsertFunction("close", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {LLVM_I32(ctx)},
            false
    ));

    write_func = M.getOrInsertFunction("write", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {LLVM_I32(ctx), llvm::Type::getInt8PtrTy(ctx), LLVM_I32(ctx)},
            false
    ));

    read_func = M.getOrInsertFunction("read", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {LLVM_I32(ctx), llvm::Type::getInt8PtrTy(ctx), LLVM_I32(ctx)},
            false
    ));

    exit_func = M.getOrInsertFunction("exit", llvm::FunctionType::get(
            llvm::Type::getVoidTy(ctx),
            {LLVM_I32(ctx)},
            false)
    );

    // Create global variable for the file descriptor
    puf_fd = M.getGlobalVariable("____puf_fd____");
    if (!puf_fd) {
        puf_fd = new llvm::GlobalVariable(
                M,
                LLVM_I32(ctx),
                false,
                llvm::GlobalValue::InternalLinkage,
                LLVM_CONST_I32(ctx, 0), "____puf_fd____"
        );
    }

    open_func = M.getOrInsertFunction("open", llvm::FunctionType::get(
            LLVM_I32(ctx),
            {
                    llvm::Type::getInt8PtrTy(ctx),
                    LLVM_I32(ctx)
            },
            false
    ));
}

void PufPatcher::insert_address_calculations(
        enrollment::EnrollData &enrollments,
        llvm::GlobalVariable *lookup_table,
        std::map<llvm::Function *, uint32_t> &lookup_table_call_mappings,
        llvm::CallGraph &call_graph
) {
    call_graph.print(llvm::outs());
    auto external_entry_points = external_nodes(call_graph);

    using MapTableFunctionToPaths = std::map<llvm::Function *, std::vector<llvm::Function *>>;
    using MapExternalPointsToTableFunctions = std::map<llvm::Function *, MapTableFunctionToPaths>;

    // all places where the control flow will check and block until the
    // necessary PUF response for the computation of the destination address is received.
    MapExternalPointsToTableFunctions mappings;
    for (auto &[func, _]: lookup_table_call_mappings) {
        for (auto &external_entry: external_entry_points) {
            auto path = find_insert_points(call_graph, external_entry, func);
            if (path.empty()) {
                continue;
            }

            // for the ecternal entry we have the shortest path to the func.
            mappings[external_entry][func] = std::move(path);
        }
    }

    for (auto &[external_func, paths_to_target_func] : mappings) {
        llvm::outs() << "External entry: " << external_func->getName().str() << "\n";
        for (auto &target_func: paths_to_target_func) {
            llvm::outs() << "\tTarget Func: " << target_func.first->getName().str() << "\n";
            for (auto &path : target_func.second) {
                llvm::outs() << "\t\t" << path->getName().str() << "\n";
            }
        }
    }

    llvm::outs() << "done" << '\n';
}

//------------------------------------------------------
//               Registration of the Plugin
//------------------------------------------------------
llvm::PassPluginLibraryInfo getPufPatcherPluginInfo() {
    return {
            LLVM_PLUGIN_API_VERSION,
            "pufpatcher",
            LLVM_VERSION_STRING,
            [](llvm::PassBuilder &PB) {
                using namespace llvm;
                PB.registerPipelineParsingCallback(
                        [&](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
                            if (Name == "pufpatcher") {
                                MPM.addPass(PufPatcher());
                                return true;
                            }
                            return false;
                        });
            }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getPufPatcherPluginInfo();
}
