#include "PufPatcher.h"

#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

static llvm::cl::opt<std::string> EnrollmentFile(
        "enrollment",
        llvm::cl::desc("Enrollment json file that was generated"),
        llvm::cl::value_desc("string"),
        llvm::cl::Optional
);

static llvm::cl::opt<std::string> OutputFile(
        "outputjson",
        llvm::cl::desc("Will output which functions will be considered for patching. no actual patching is done"),
        llvm::cl::value_desc("string"),
        llvm::cl::Optional
);

static llvm::cl::opt<std::string> InputFile(
        "inputjson",
        llvm::cl::desc("Will patch the IR based from the information of the compiled binary"),
        llvm::cl::value_desc("string"),
        llvm::cl::Optional
);

llvm::PreservedAnalyses PufPatcher::run(llvm::Module &M, llvm::ModuleAnalysisManager &AM) {
    init_deps(M);

    // Store which functions are we considering in this LLVM pass
    // for double-checking which of the functions will be in the binary.
    if (!OutputFile.empty()) {
        // collect functions for which we have definitions.
        // These are the function we will patch.
        std::vector<std::string> func_names;
        for (auto &f: M) {
            if (f.isIntrinsic() || f.isDeclaration() || f.empty()) {
                continue;
            }
            func_names.push_back(f.getName().str());
        }
        crossover::write_func_requests(OutputFile, func_names);
        return llvm::PreservedAnalyses::all();
    }

    auto enrollments = crossover::read_enrollment_data(EnrollmentFile);

    auto table = crossover::read_func_metadata(InputFile);
    if (table.empty()) {
        llvm::outs() << "InputFile empty: patching skeleton only" << '\n';
        return llvm::PreservedAnalyses::all();
    }

    std::vector<llvm::Function *> functions_to_patch;
    for (auto &f: M) {
        if (table.find(f.getName().str()) != table.end()) {
            functions_to_patch.push_back(&f);
        }
    }

    // Analyse call graph before replacing with indirect calls and before adding
    // PUF thread.
    auto call_graph = llvm::CallGraphAnalysis().run(M, AM);

    // Find all external entry points into the IR module.
    auto external_entry_points = find_all_external_entry_points(M, call_graph);

    // Creates a global array where the PUF measurements will be stored.
    auto puf_array = create_puf_array(M, enrollments);

    // Replaces all calls/invokes in the collected functions and creates a lookup table
    // where each function has it place which will be then computed when receiving the correct PUF response.
    // After this function only the call_graph should be used for identifying the calls.
    auto lookup_table = replace_calls_with_lookup_table(M, functions_to_patch);

    // Given the identified entry points into the IR module.
    // determines at which places the functions addresses in
    // the lookup table should be computed.
    insert_address_calculations(
            table,
            enrollments,
            puf_array,
            lookup_table,
            call_graph,
            external_entry_points
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
    checksum.run(
            M,
            functions_to_patch,
            table
    );

    return llvm::PreservedAnalyses::none();
}

std::pair<
        llvm::GlobalVariable *,
        std::map<llvm::Function *, uint32_t>
>
PufPatcher::replace_calls_with_lookup_table(
        llvm::Module &M,
        const std::vector<llvm::Function *> &funcs
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
        const std::unordered_map<std::string, crossover::FunctionInfo> &compiled_functions_metadata,
        const crossover::EnrollData &enrollments,
        const std::pair<llvm::GlobalVariable *, size_t> &puf_array,
        const std::pair<llvm::GlobalVariable *, std::map<llvm::Function *, uint32_t>> &lookup_table,
        const llvm::CallGraph &call_graph,
        const std::set<llvm::Function *> &external_entry_points
) {
    auto &[look_up_table_global, lookup_table_call_mappings] = lookup_table;

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

    // For each function that has an index in the lookup table
    // we need to find all the paths that call this function from all of
    // the possible external entry points into the IR module.
    // The path will represent all the functions where the insertion is possible
    // before making a call via an indirect pointer into the lookup table.
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

    // Debug Print collected information.
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
    std::map<MapKey, std::vector<FunctionCallReplacementInfo>, MapKeyComparison> collected_replacement_info;

    // For each external entry point and the collected paths from those external entry points
    // We randomly choose a function from that path where we will insert the blocking until the puf.
    // If the same function is selected multiple times (possible, since multiple external paths
    // can call that function) it will wait until all the requested responses of PUF will collected.
    for (auto &[external_entry, paths]: mappings) {
        uint32_t seed = std::accumulate(external_entry.key.begin(), external_entry.key.end(), 0);
        auto rng = RandomRNG(seed);

        // collect depth sizes.
        std::set<size_t> depth_levels;
        for (auto &item: paths) depth_levels.insert(item.second.size());
        // assign puf index to wait on based on level of depth a path has
        // if there are multiple levels of depth then each level waits on a different
        // puf response, the bigger the depth the later the response of the puf.
        size_t number_of_depths = depth_levels.size();
        size_t puf_array_size = puf_array.second;
        size_t number_of_depths_unlocked_per_puf_response = int(ceil(float(number_of_depths) / float(puf_array_size)));

        // create a map that maps each depth level to a puf index
        size_t unlocked = 0;
        size_t mapped_puf_index = 0;
        std::map<size_t, size_t> depth_to_puf_response;
        for (size_t item : depth_levels) {
            depth_to_puf_response[item] = mapped_puf_index;
            unlocked++;
            if (unlocked % number_of_depths_unlocked_per_puf_response == 0) {
                mapped_puf_index++;
            }
        }

        for (auto &[function_call_to_replace, path]: paths) {
            assert(compiled_functions_metadata.find(function_call_to_replace.key) != compiled_functions_metadata.end());
            uint32_t function_call_to_replace_address = compiled_functions_metadata.at(
                    function_call_to_replace.key).base.offset;

            int32_t puf_arr_index = depth_to_puf_response[path.size()];
            const crossover::Enrollment *enrollment = enrollments.request_at(puf_arr_index);
            assert(enrollment != nullptr);
            // randomly choose at which function the instruction will be inserted.
            auto *function = *RandomElementRNG(path.begin(), path.end(), rng);

            // so that we don't have duplicates in the same function.
            bool exists = std::any_of(
                    checks_inserted[function_call_to_replace.function].begin(),
                    checks_inserted[function_call_to_replace.function].end(),
                    [&](const auto &check) { return check == function; }
            );
            if (exists) {
                continue;
            }
            checks_inserted[function_call_to_replace.function].push_back(function);

            collected_replacement_info[MapKey{function, function->getName().str()}].emplace_back(
                    FunctionCallReplacementInfo{
                            function_call_to_replace.function,
                            puf_arr_index,
                            enrollment->auth_value,
                            function_call_to_replace_address
                    }
            );
        }
    }

    // Finally, patch the functions.
    for (auto &[key, info]: collected_replacement_info) {
        // Debug print.
        // These addresses should exactly match the ones in the binary when compiled.
        // If they don't match, some of the inserted operations are not deterministic
        // and the compiler changes the compiled code in a non-deterministic way.
        for (auto &v: info) {
            llvm::outs() << "Function: " << key.function->getName().str() << "\n"
                         << "\t" << "Will calculate address: " << v.function_call_to_replace_address << "\n"
                         << "\t" << "To access function: " << v.funcion_call_to_replace->getName().str() << "\n"
                         << "\t" << "Will use PUF: (idx) " << v.puff_arr_index << " (value) "
                         << v.puff_response_at_offset
                         << "\n"
                         << "\t" << "Will replace address at index: "
                         << lookup_table.second.at(v.funcion_call_to_replace) << "\n";
        }

        generate_block_until_puf_response(
                lookup_table,
                key.function,
                info,
                puf_array,
                compiled_functions_metadata
        );
    }
}

void PufPatcher::generate_block_until_puf_response(
        const std::pair<llvm::GlobalVariable *, std::map<llvm::Function *, uint32_t>> &lookup_table,
        llvm::Function *const function_to_add_code,
        const std::vector<FunctionCallReplacementInfo> &replacement_info,
        const std::pair<llvm::GlobalVariable *, size_t> &puf_array,
        const std::unordered_map<std::string, crossover::FunctionInfo> &compiled_functions_metadata
) {
    // Implement
    // puf_offsets[...];
    // lookup_table_offsets[...];
    // reference_values[...];
    // offset_reader = 0x0;
    // checksum = 0x0;
    // while (puf_array[puff_offsets[offset_reader]] == 0) {
    //  checksum += hash(...);
    // }
    // lookup_table[lookup_table_offsets[offset_reader]] = puf_array[puff_offsets[offset_reader]] + reference_values[offset_reader] + checksum;
    auto &ctx = function_to_add_code->getParent()->getContext();

    auto *generated_checksum_func = generate_checksum_func(*function_to_add_code->getParent());

    // choose a random function the checksum will be calculated over
    std::string s = function_to_add_code->getName().str();
    uint32_t seed = std::accumulate(s.begin(), s.end(), 0);
    auto &chosen_function = *RandomElementRNG(
            compiled_functions_metadata.begin(),
            compiled_functions_metadata.end(),
            RandomRNG(seed)
    );

    auto &function_entry_block = function_to_add_code->getEntryBlock();

    auto new_entry_block = llvm::BasicBlock::Create(
            ctx,
            "new_entry_block",
            function_to_add_code,
            &function_entry_block
    );

    llvm::IRBuilder<> Builder(new_entry_block);

    // Create Start address
    auto start_address_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    Builder.CreateStore(LLVM_CONST_I32(ctx, chosen_function.second.base.offset), start_address_ptr);
    // Create Count
    auto count_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    Builder.CreateStore(LLVM_CONST_I32(ctx, chosen_function.second.instruction_count), count_ptr);
    // Create Constant
    auto constant_m_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    Builder.CreateStore(LLVM_CONST_I32(ctx, chosen_function.second.constant), constant_m_ptr);

    // Create checksum = 0x0;
    auto *checksum_ptr = Builder.CreateAlloca(LLVM_I32(ctx));
    Builder.CreateStore(LLVM_CONST_I32(ctx, 0), checksum_ptr);

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
        lookup_table_offsets_data.push_back(LLVM_CONST_I32(ctx, lookup_table.second.at(info.funcion_call_to_replace)));
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
            puf_array.first->getValueType(),
            puf_array.first,
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

    // if 0 calc checksum.
    Builder.SetInsertPoint(false_block);
    Builder.CreateCall(generated_checksum_func, {
            checksum_ptr,
            start_address_ptr,
            count_ptr,
            constant_m_ptr
    });
    // loop back
    Builder.CreateBr(loop_header_bb);

    // If not 0
    // calculate lookup_table[func_index] = puf_response + ref_value + checksum
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

    // lookup_table[lookup_table_offsets[offset_reader]] = puf_array[puf_offsets[offset_reader]] + reference_values[offset_reader] + checksum
    Builder.CreateStore(
            Builder.CreateAdd(
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
                    Builder.CreateLoad(LLVM_U32(ctx), checksum_ptr)
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
            Builder.CreateLoad(LLVM_I32(ctx), offsets_reader_ptr),
            LLVM_CONST_I32(ctx, replacement_info.size())
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
