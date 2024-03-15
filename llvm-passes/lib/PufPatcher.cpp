#include "PufPatcher.h"

#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/InlineAsm.h"

static llvm::cl::opt<std::string> EnrollmentFile(
        "enrollment",
        llvm::cl::desc("Enrollment json file that was generated"),
        llvm::cl::value_desc("string"),
        llvm::cl::Required
);

static llvm::cl::opt<std::string> OutputFile(
        "outputjson",
        llvm::cl::desc(
                "When enabled the LLVM pass will patch the .ll files with the necessary instructions, but without functioning properly."
                " It will generate an output json file that contains all of the patched functions"),
        llvm::cl::value_desc("string"),
        llvm::cl::Optional
);

static llvm::cl::opt<std::string> InputFile(
        "inputjson",
        llvm::cl::desc(
                "When given an input file the LLVM pass will patch the .ll files with the necessary instructions and with the values"
                "present in that input file."),
        llvm::cl::value_desc("string"),
        llvm::cl::Optional
);

llvm::PreservedAnalyses PufPatcher::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
    init_deps(M);
    auto enrollments = crossover::read_enrollment_data(EnrollmentFile);

    std::vector<std::string> func_names;
    std::vector<llvm::Function *> functions_to_patch;
    for (auto &f: M) {
        if (f.isIntrinsic() || f.isDeclaration() || f.empty()) {
            continue;
        }
        functions_to_patch.push_back(&f);
        func_names.push_back(f.getName().str());
    }

    // Analyse call graph before replacing with indirect calls and before adding
    // PUF thread.
    auto call_graph = llvm::CallGraphAnalysis().run(M, AM);

    // Creates a global array where the PUF measurements will be stored.
    auto puf_array = create_puf_array(M, enrollments);

    // Replaces all calls/invokes in the collected functions and creates a lookup table
    // where each function has it place which will be then computed when receiving the correct PUF response.
    // After this function only the call_graph should be used for identifying the calls.
    auto lookup_table = replace_calls_with_lookup_table(M, functions_to_patch);

    // Store which functions are we considering in this LLVM pass
    // and alongisde it also the calls that are made within those functions.
    // The output JSON will be used by other program to generate the offsets
    // and other data within the ELF binary.
    if (!OutputFile.empty()) {
        crossover::write_func_requests(
                OutputFile,
                func_names,
                lookup_table
        );
    }

    auto table = crossover::read_func_metadata(InputFile);
    if (table.empty()) {
        llvm::outs() << "InputFile empty: patching skeleton only" << '\n';
    }

    // Identifies all possibly external entry points and determines at which places
    // the functions addresses in the lookup table should be computed.
    insert_address_calculations(
            table,
            enrollments,
            puf_array,
            lookup_table,
            call_graph
    );

    // open the /dev/puf in a ctor
    // It will open the device, write the enrollment data to it.
    auto ctor = puf_open_ctor(M, enrollments, global_variables.puf_fd);

    // close /dev/puf in a dtor
    puf_close_dtor(M, global_variables.puf_fd);

    // add code that spawns a detached thread that will perform the PUF readings.
    // Function call added will not be in the lookup table created above, this is by design.
    spawn_puf_thread(M, puf_array, ctor, enrollments);

    // add checksums of the patched functions.
//    checksum.run(M, funcs);

    return llvm::PreservedAnalyses::none();
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

    // for each function collect all call instructions that we know we can replace.
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
                        bool exists = std::any_of(
                                funcs.begin(), funcs.end(),
                                [&](const auto &check) { return check == calle; }
                        );
                        if (exists) { // only consider this call if the address is known at compile time.
                            mappings[f->getName().str()].push_back(is_call);
                        }
                    }
                }
            }
        }
    }

    // group calls to replace and their occurrences.

    // to guarantee the same traversal each time the keys in the maps need to be
    // deterministic on each run.
    struct MapKey {
        llvm::Function *function = nullptr;
        std::string key;
    };

    struct MapKeyComparison {
        bool operator()(const MapKey &lhs, const MapKey &rhs) const {
            return lhs.key < rhs.key;
        }
    };
    std::map<MapKey, std::vector<llvm::CallBase *>, MapKeyComparison> group_calls;
    for (auto &[_, calls]: mappings) {
        for (auto &call: calls) {
            group_calls[MapKey{call->getCalledFunction(), call->getCalledFunction()->getName().str()}].push_back(call);
        }
    }

    // create a global lookup table of functions addresses.
    size_t lookup_table_size = group_calls.size();

    auto &ctx = M.getContext();
    std::vector<llvm::Constant *> lookup_table_data(lookup_table_size, LLVM_CONST_I32(ctx, 0));

    auto lookup_table_typ = llvm::ArrayType::get(LLVM_I32(ctx), lookup_table_size);
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
    for (auto &[key, calls]: group_calls) {
        func_to_lookup_idx[key.function] = idx;
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

void PufPatcher::insert_address_calculations(
        std::unordered_map<std::string, crossover::Function> &compiled_functions_metadata,
        crossover::EnrollData &enrollments,
        std::pair<llvm::GlobalVariable *, size_t> &puf_array,
        std::pair<llvm::GlobalVariable *, std::map<llvm::Function *, uint32_t>> &lookup_table,
        llvm::CallGraph &call_graph
) {
    auto &[look_up_table_global, lookup_table_call_mappings] = lookup_table;
    auto external_entry_points = external_nodes(call_graph);

    // to guarantee the same traversal each time the keys in the maps need to be
    // deterministic on each run.
    struct MapKey {
        llvm::Function *function = nullptr;
        std::string key;
    };

    struct MapKeyComparison {
        bool operator()(const MapKey &lhs, const MapKey &rhs) const {
            return lhs.key < rhs.key;
        }
    };

    using MapTableFunctionToPaths = std::map<MapKey, std::vector<llvm::Function *>, MapKeyComparison>;
    using MapExternalPointsToTableFunctions = std::map<MapKey, MapTableFunctionToPaths, MapKeyComparison>;

    // all places where the control flow will check and block until the
    // necessary PUF response for the computation of the destination address is received.
    MapExternalPointsToTableFunctions mappings;
    for (auto &[func, _]: lookup_table_call_mappings) {
        for (auto &external_entry: external_entry_points) {
            auto path = find_insert_points(call_graph, external_entry, func);
            if (path.empty()) {
                continue;
            }

            MapKey external_entry_key = MapKey{external_entry, external_entry->getName().str()};
            MapKey func_key = MapKey{func, func->getName().str()};
            mappings[external_entry_key][func_key] = std::move(path);
        }
    }

    for (auto &[external_func, paths_to_target_func]: mappings) {
        llvm::outs() << "External entry: " << external_func.key << "\n";
        for (auto &target_func: paths_to_target_func) {
            llvm::outs() << "\tTarget Func: " << target_func.first.key << "\n";
            for (auto &path: target_func.second) {
                llvm::outs() << "\t\t" << path->getName().str() << "\n";
            }
        }
    }

    // keep track for which PUF array index each function had checks inserted.
    std::map<llvm::Function *, std::vector<llvm::Function *>> checks_inserted;
    std::map<MapKey, std::vector<FunctionCallReplacementInfo>, MapKeyComparison> tt;
    for (auto &[external_entry, paths]: mappings) {
        uint32_t seed = std::accumulate(external_entry.key.begin(), external_entry.key.end(), 0);
        auto rng = RandomRNG(seed);

        for (auto &[function_call_to_replace, path]: paths) {
            uint32_t function_call_to_replace_address =
                    compiled_functions_metadata[function_call_to_replace.key].base.offset == 0 ?
                    uint32_t(uintptr_t(function_call_to_replace.function)) : // take a placeholder address.
                    compiled_functions_metadata[function_call_to_replace.key].base.offset;
            // randomly choose at which PUF response in the PUF array this path will wait.
            int32_t puf_arr_index = random_i32(puf_array.second, rng);
            crossover::Enrollment *enrollment = enrollments.request_at(puf_arr_index);
            assert(enrollment != nullptr);
            // randomly choose at which function the instruction will be inserted.
            auto *function = *RandomElementRNG(path.begin(), path.end(), rng);

            bool exists = std::any_of(
                    checks_inserted[function_call_to_replace.function].begin(),
                    checks_inserted[function_call_to_replace.function].end(),
                    [&](const auto &check) { return check == function; }
            );
            if (exists) {
                continue;
            }
            checks_inserted[function_call_to_replace.function].push_back(function);

            tt[MapKey{function, function->getName().str()}].emplace_back(FunctionCallReplacementInfo{
                    function_call_to_replace.function,
                    puf_arr_index,
                    enrollment->auth_value,
                    function_call_to_replace_address
            });

        }
    }

    for (auto &[key, v]: tt) {
        for (auto &v: v) {
            llvm::outs() << "Function: " << key.function->getName().str() << "\n"
                         << "\t" << "Will calculate address: " << v.function_call_to_replace_address << "\n"
                         << "\t" << "To access function: " << v.funcion_call_to_replace->getName().str() << "\n"
                         << "\t" << "Will use PUF: (idx) " << v.puff_arr_index << " (value) "
                         << v.puff_response_at_offset
                         << "\n"
                         << "\t" << "Will replace address at index: "
                         << lookup_table.second[v.funcion_call_to_replace] << "\n";
        }

        generate_block_until_puf_response(
                lookup_table,
                key.function,
                v,
                puf_array.first
        );
    }
}

void PufPatcher::generate_block_until_puf_response(
        std::pair<llvm::GlobalVariable *, std::map<llvm::Function *, uint32_t>> &lookup_table,
        llvm::Function *function_to_add_code,
        std::vector<FunctionCallReplacementInfo> &replacement_info,
        llvm::GlobalVariable *puf_array
) {
    // Implement
    // puf_offsets[...]
    // lookup_table_offsets[...]
    // reference_values[...]
    // offset_reader = 0x0;
    // while (puf_array[puff_offsets[offset_reader]] == 0) {
    //  keep busy to block.
    // }
    // lookup_table[lookup_table_offsets[offset_reader]] = puf_array[puff_offsets[offset_reader]] + reference_values[offset_reader];
    auto &ctx = function_to_add_code->getParent()->getContext();

    auto &function_entry_block = function_to_add_code->getEntryBlock();

    auto new_entry_block = llvm::BasicBlock::Create(
            ctx,
            "new_entry_block",
            function_to_add_code,
            &function_entry_block
    );

    llvm::IRBuilder<> Builder(new_entry_block);

    // Create puff_offsets[...]
    auto puf_offsets_typ = llvm::ArrayType::get(LLVM_I32(ctx), replacement_info.size());
    auto *puf_offsets_ptr = Builder.CreateAlloca(puf_offsets_typ);

    // Create lookup_table_offsets[...]
    auto lookup_table_offsets_typ = llvm::ArrayType::get(LLVM_I32(ctx), replacement_info.size());
    auto *lookup_table_offsets_ptr = Builder.CreateAlloca(lookup_table_offsets_typ);

    // Create reference_values[...]
    auto reference_values_typ = llvm::ArrayType::get(LLVM_I32(ctx), replacement_info.size());
    auto *reference_value_ptr = Builder.CreateAlloca(reference_values_typ);

    std::vector<llvm::Constant *> puf_offsets_data;
    std::vector<llvm::Constant *> lookup_table_offsets_data;
    std::vector<llvm::Constant *> reference_values_data;

    for (auto &info: replacement_info) {
        assert(lookup_table.second.find(info.funcion_call_to_replace) != lookup_table.second.end());
        puf_offsets_data.push_back(LLVM_CONST_I32(ctx, info.puff_arr_index));
        lookup_table_offsets_data.push_back(LLVM_CONST_I32(ctx, lookup_table.second[info.funcion_call_to_replace]));
        reference_values_data.push_back(
                LLVM_CONST_I32(ctx, info.function_call_to_replace_address - info.puff_response_at_offset));
    }

    Builder.CreateStore(llvm::ConstantArray::get(puf_offsets_typ, puf_offsets_data), puf_offsets_ptr);
    Builder.CreateStore(llvm::ConstantArray::get(lookup_table_offsets_typ, lookup_table_offsets_data),
                        lookup_table_offsets_ptr);
    Builder.CreateStore(llvm::ConstantArray::get(reference_values_typ, reference_values_data), reference_value_ptr);

    // Create offset_reader = 0x0
    auto *offsets_reader_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    Builder.CreateStore(LLVM_CONST_I32(ctx, 0), offsets_reader_ptr);

    // Loop header
    auto *loop_header_bb = llvm::BasicBlock::Create(ctx, "loop_header", function_to_add_code, &function_entry_block);
    Builder.CreateBr(loop_header_bb);
    Builder.SetInsertPoint(loop_header_bb);

    // Load current index to wait on.
    // puf_array[puf_offsets[offset_reader]]
    auto puf_array_ptr = Builder.CreateInBoundsGEP(
            puf_array->getValueType(),
            puf_array,
            {
                    LLVM_CONST_I32(ctx, 0),
                    Builder.CreateLoad(
                            LLVM_I32(ctx),
                            Builder.CreateInBoundsGEP(
                                    puf_offsets_typ,
                                    puf_offsets_ptr,
                                    {
                                            LLVM_CONST_I32(ctx, 0),
                                            Builder.CreateLoad(LLVM_I32(ctx), offsets_reader_ptr)
                                    }
                            )
                    )

            }
    );

    // check if 0
    auto condition = Builder.CreateICmpEQ(Builder.CreateLoad(LLVM_U32(ctx), puf_array_ptr), LLVM_CONST_I32(ctx, 0));
    auto true_block = llvm::BasicBlock::Create(ctx, "puf_loaded", function_to_add_code, &function_entry_block);
    auto false_block = llvm::BasicBlock::Create(ctx, "puf_not_loaded", function_to_add_code, &function_entry_block);
    Builder.CreateCondBr(condition, false_block, true_block);

    // if 0 sleep 5 go back.
    Builder.SetInsertPoint(false_block);
    Builder.CreateCall(lib_c_dependencies.sleep_func, {LLVM_CONST_I32(ctx, 5)});
    Builder.CreateBr(loop_header_bb);

    // If not 0
    // calculate lookup_table[func_index] = puf_response + ref_value
    // increment to process next element
    // else we are done.
    Builder.SetInsertPoint(true_block);
    // lookup_table[lookup_table_offsets[offset_reader]]
    auto lookup_table_ptr = Builder.CreateInBoundsGEP(
            lookup_table.first->getValueType(),
            lookup_table.first,
            {
                    LLVM_CONST_I32(ctx, 0),
                    Builder.CreateLoad(
                            LLVM_I32(ctx),
                            Builder.CreateInBoundsGEP(
                                    lookup_table_offsets_typ,
                                    lookup_table_offsets_ptr,
                                    {
                                            LLVM_CONST_I32(ctx, 0),
                                            Builder.CreateLoad(LLVM_I32(ctx), offsets_reader_ptr)
                                    }
                            )
                    )
            }
    );

    // lookup_table[lookup_table_offsets[offset_reader]] = puf_array[puf_offsets[offset_reader]] + reference_values[offset_reader]
    Builder.CreateStore(
            Builder.CreateAdd(
                    Builder.CreateLoad(LLVM_U32(ctx), puf_array_ptr),
                    Builder.CreateLoad(
                            LLVM_U32(ctx),
                            Builder.CreateInBoundsGEP(
                                    reference_values_typ,
                                    reference_value_ptr,
                                    {
                                            LLVM_CONST_I32(ctx, 0),
                                            Builder.CreateLoad(LLVM_U32(ctx), offsets_reader_ptr)
                                    }
                            )
                    )
            ),
            lookup_table_ptr
    );

    // offset_reader = offset_reader + 1
    Builder.CreateStore(
            Builder.CreateAdd(
                    Builder.CreateLoad(LLVM_I32(ctx), offsets_reader_ptr),
                    LLVM_CONST_I32(ctx, 1)
            ),
            offsets_reader_ptr
    );

    // If still items to process go back to header else continue
    condition = Builder.CreateICmpEQ(
            Builder.CreateLoad(LLVM_I32(ctx), offsets_reader_ptr), LLVM_CONST_I32(ctx, replacement_info.size())
    );
    Builder.CreateCondBr(condition, &function_entry_block, loop_header_bb);

    assert(&function_to_add_code->getEntryBlock() == new_entry_block);
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
