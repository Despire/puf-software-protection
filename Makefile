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
	cd ./puf-hds && $(CARGO) build --release
	$(MAKE) -C ./arch_emulator
	$(MAKE) patch

patch: 
	mkdir -p .llvm_ir_cache
	$(MAKE) generate-ir
	cp ./arch_emulator/volume/example/target/release/deps/*.bc ./.llvm_ir_cache
	$(MAKE) dump-functions
	$(MAKE) compile
	@while true; do \
		$(MAKE) generate-metadata; \
		cp ./.llvm_ir_cache/*.bc ./arch_emulator/volume/example/target/release/deps/; \
		$(MAKE) patch-segments; \
		$(MAKE) compile; \
		$(MAKE) check-binary-offsets && break; \
	done
	$(MAKE) checksum-patch-binary
	echo "DONE YOU CAN NOW USE YOUR BINARY"
	rm -r .llvm_ir_cache/

generate-ir:
	docker exec $(IMAGE_ID) sh -c "cd ./example && ./s.sh"

dump-functions:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.bc' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libPufPatcher$(LIB_EXT) -passes=pufpatcher -outputjson=./out.json -S $${file} -o $${file}; \
	done

patch-segments:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.bc' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libPufPatcher$(LIB_EXT) -passes=pufpatcher -enrollment=./enroll.json -inputjson=./metadata.json -S $${file} -o $${file}; \
	done

generate-metadata:
	./elf-parser/target/release/elf-parser read ./out.json ./metadata.json ./arch_emulator/volume/example/target/release/deps/program

checksum-patch-binary:
	./elf-parser/target/release/elf-parser patch ./out.json ./arch_emulator/volume/example/target/release/deps/program

check-binary-offsets:
	./elf-parser/target/release/elf-parser check ./out.json ./metadata.json ./arch_emulator/volume/example/target/release/deps/program

compile:
	docker exec $(IMAGE_ID) sh -c "cd ./example && ./s.sh compile"

execute:
	docker exec $(IMAGE_ID) sh -c "./example/target/release/deps/program"

dump-s:
	docker exec $(IMAGE_ID) sh -c "objdump -S ./example/target/release/deps/program" | less

copy-llvm-ir:
	cp ./arch_emulator/volume/example/target/release/deps/*ll .

clean:
	$(MAKE) -C ./arch_emulator clean
	rm -rf ./arch_emulator/volume/example/target
	rm -rf $(CMAKE_OUT)
	rm ./out.json
	rm ./metadata.json
	rm -r .llvm_ir_cache/


.PHONY: all generate-ir compile generate-json-checksum patch generate-metadata patch-llvm-ir-from-metadata
