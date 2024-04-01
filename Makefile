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
	$(MAKE) patch-empty
	$(MAKE) compile
	$(MAKE) fix-up-functions
	cp ./.build_cache/*.bc ./arch_emulator/volume/example/target/release/deps
	$(MAKE) patch-puf
	mv replacements.json ./.build_cache/
	$(MAKE) compile
	$(MAKE) patch-binary
	echo "DONE YOU CAN NOW USE YOUR BINARY"
	rm -r ./.build_cache/

patch-prefix:
	mkdir -p .build_cache
	$(MAKE) generate-ir
	cp ./arch_emulator/volume/example/target/release/deps/*.bc ./.build_cache
	$(MAKE) patch-empty-prefix
	$(MAKE) compile
	$(MAKE) fix-up-functions
	cp ./.build_cache/*.bc ./arch_emulator/volume/example/target/release/deps
	$(MAKE) patch-puf-prefix
	mv replacements.json ./.build_cache/
	$(MAKE) compile
	$(MAKE) patch-binary
	echo "DONE YOU CAN NOW USE YOUR BINARY"
	rm -r ./.build_cache/

generate-ir:
	docker exec $(IMAGE_ID) sh -c "cd ./example && ./s.sh"

patch-empty-prefix:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.bc' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libPufPatcher$(LIB_EXT) -passes=pufpatcher -enrollment=./enrollments/enroll.json -outputjson=./.build_cache/functions_to_patch.json -inputjson=./.build_cache/functions_to_patch.json -prefix=handle -S $${file} -o $${file}; \
	done

patch-puf-prefix:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.bc' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libPufPatcher$(LIB_EXT) -passes=pufpatcher -enrollment=./enrollments/enroll.json -inputjson=./.build_cache/functions_to_patch.json -prefix=handle -S $${file} -o $${file}; \
	done

patch-empty:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.bc' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libPufPatcher$(LIB_EXT) -passes=pufpatcher -enrollment=./enrollments/enroll.json -outputjson=./.build_cache/functions_to_patch.json -inputjson=./.build_cache/functions_to_patch.json -S $${file} -o $${file}; \
	done

patch-puf:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.bc' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libPufPatcher$(LIB_EXT) -passes=pufpatcher -enrollment=./enrollments/enroll.json -inputjson=./.build_cache/functions_to_patch.json -S $${file} -o $${file}; \
	done

# this will read out the functions that the LLVM pass wants to patch and see which will be present in 
# the final binary after all the optimaztions. and will overwrite the functions_to_patch.json with the result.
fix-up-functions: 
	./elf-parser/target/release/elf-parser read ./.build_cache/functions_to_patch.json ./arch_emulator/volume/example/target/release/deps/program

# This will replace all the markers with actuall data.
patch-binary:
	./elf-parser/target/release/elf-parser patch ./.build_cache/functions_to_patch.json ./.build_cache/replacements.json ./arch_emulator/volume/example/target/release/deps/program

compile:
	docker exec $(IMAGE_ID) sh -c "cd ./example && ./s.sh compile"

clean:
	rm -rf ./arch_emulator/volume/example/target
	rm -rf $(CMAKE_OUT)
	rm -r ./elf-parser/target
	$(MAKE) -C ./enrollments clean
	$(MAKE) -C ./arch_emulator clean
	rm -r .build_cache/
	
.PHONY: all generate-ir compile patch-binary generate-metadata patch-puf patch-empty wait-for-init
