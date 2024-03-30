UNAME_S := $(shell uname -s)

LIB_EXT := .so
# Check if the platform is macOS (Darwin) and adjust the library extension
ifeq ($(UNAME_S),Darwin)
	LIB_EXT := .dylib
endif


# MODIFY THIS BASED WHERE YOU HAVE opt
LLVM_OPT=/opt/homebrew/opt/llvm@17/bin/opt

CARGO=cargo
CMAKE=cmake
CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=Debug 
CMAKE_OUT=./llvm-passes/cmake-build-debug
IMAGE_ID=$(shell docker ps --filter "ancestor=bbb-cpu-arch" -q)

all:
	$(CMAKE) $(CMAKE_FLAGS) -S ./llvm-passes -B $(CMAKE_OUT)
	$(MAKE) -C $(CMAKE_OUT)
	cd ./elf-parser && $(CARGO) build --release
	cd ./enrollments/enroll && $(CARGO) build --release
	$(MAKE) -C ./enrollments enroll
	$(MAKE) -C ./arch_emulator
	@echo "waiting for container to be initialized"
	$(MAKE) wait-for-init
	@echo "done"

wait-for-init:
	until [ -f ./arch_emulator/volume/.initialized ]; do \
		echo "setup.sh is runnning, checking every 5 sec until it finishes"; \
		sleep 5; \
	done

patch: 
	mkdir -p .build_cache
	$(MAKE) generate-ir
	cp ./arch_emulator/volume/example/target/release/deps/*.bc ./.build_cache
	$(MAKE) dump-functions
	$(MAKE) compile
	@while true; do \
		$(MAKE) generate-metadata; \
		cp ./.build_cache/*.bc ./arch_emulator/volume/example/target/release/deps/; \
		$(MAKE) patch-segments; \
		$(MAKE) compile; \
		$(MAKE) check-binary-offsets && break; \
	done
	$(MAKE) checksum-patch-binary
	echo "DONE YOU CAN NOW USE YOUR BINARY"
	rm -r .build_cache/

generate-ir:
	docker exec $(IMAGE_ID) sh -c "cd ./example && ./s.sh"

dump-functions:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.bc' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libPufPatcher$(LIB_EXT) -passes=pufpatcher -outputjson=./.build_cache/functions_to_patch.json -S $${file} -o $${file}; \
	done

patch-segments:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.bc' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libPufPatcher$(LIB_EXT) -passes=pufpatcher -enrollment=./enrollments/enroll.json -inputjson=./.build_cache/functions_to_patch_metadata.json -S $${file} -o $${file}; \
	done

# this will read out the functions that the LLVM pass wants to patch and see which will be present in 
# the final binary after all the optimaztions. and will overwrite the functions_to_patch.json with the result.
generate-metadata: 
	./elf-parser/target/release/elf-parser read ./.build_cache/functions_to_patch.json ./.build_cache/functions_to_patch_metadata.json ./arch_emulator/volume/example/target/release/deps/program

# This will replace all the markers with actuall function data.
checksum-patch-binary:
	./elf-parser/target/release/elf-parser patch ./.build_cache/functions_to_patch.json ./arch_emulator/volume/example/target/release/deps/program

# check if the offset from the requested file match the offsets in the binary
check-binary-offsets:
	./elf-parser/target/release/elf-parser check ./.build_cache/functions_to_patch.json ./.build_cache/functions_to_patch_metadata.json ./arch_emulator/volume/example/target/release/deps/program

compile:
	docker exec $(IMAGE_ID) sh -c "cd ./example && ./s.sh compile"

clean:
	rm -rf ./arch_emulator/volume/example/target
	rm -rf $(CMAKE_OUT)
	rm -r ./elf-parser/target
	$(MAKE) -C ./enrollments clean
	$(MAKE) -C ./arch_emulator clean
	rm -r .build_cache/
	
.PHONY: all generate-ir compile check-binary-offsets checksum-patch-binary generate-metadata patch-segments dump-functions wait-for-init
