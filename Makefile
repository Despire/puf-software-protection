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
	$(MAKE) -C ./arch_emulator
	cd ./elf-parser && $(CARGO) build --release
	cd ./puf-hds && $(CARGO) build --release

patch: 
	$(MAKE) generate-ir
	$(MAKE) patch-llvm-ir-empty
	$(MAKE) compile
	$(MAKE) generate-metadata
	$(MAKE) generate-ir
	$(MAKE) patch-llvm-ir-segments
	$(MAKE) compile
	$(MAKE) generate-metadata
	$(MAKE) generate-ir
	$(MAKE) patch-llvm-ir-parity
	$(MAKE) compile

generate-ir:
	docker exec $(IMAGE_ID) sh -c "cd ./example && ./s.sh"

patch-llvm-ir-empty:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.ll' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libChecksum$(LIB_EXT) -passes=checksum -out=./out.json -S $${file} -o $${file}; \
	done

patch-llvm-ir-segments:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.ll' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libChecksum$(LIB_EXT) -passes=checksum -in=./metadata.json -pskip=true -S $${file} -o $${file}; \
	done

patch-llvm-ir-parity:
	find ./arch_emulator/volume/example/target/release/deps/ -name '*.ll' | while read -r file; do \
		$(LLVM_OPT) -load-pass-plugin $(CMAKE_OUT)/lib/libChecksum$(LIB_EXT) -passes=checksum -in=./metadata.json -S $${file} -o $${file}; \
	done

generate-metadata:
	./elf-parser/target/release/elf-parser ./out.json ./metadata.json ./arch_emulator/volume/example/target/release/deps/program

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


.PHONY: all generate-ir compile generate-json-checksum patch generate-metadata patch-llvm-ir-from-metadata
