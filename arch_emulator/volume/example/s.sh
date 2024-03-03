#/bin/bash

set +x

llvm_path=../clang+llvm-17.0.6-armv7a-linux-gnueabihf/bin/
output_path=./target/release/deps
HOST_VAR=$(rustc --version --verbose | grep 'host:' | awk '{print $2}')
PARENT_DIR="$HOME/.rustup/toolchains"
ARCH_DIR=$(find "$PARENT_DIR" -type d -name "*$HOST_VAR*" -print -quit)

if [ -n "$ARCH_DIR" ]; then
  echo "Directory containing $HOST_VAR found: $ARCH_DIR"
else
  echo "Directory containing $HOST_VAR not found."
fi

LIB_DIR=$ARCH_DIR/lib/rustlib/$HOST_VAR/lib
RUSTLIBS=$(find ${LIB_DIR}/ -name "*.rlib")

echo "Using $LIB_DIR for rustlib"
echo "Found RUSTLIBS $RUSTLIBS"

if [ $# -eq 0 ]; then
    echo "Generating LLMV IR..."
    RUSTFLAGS="-C save-temps --emit=llvm-ir" cargo build --release
    rm ${output_path}/*no-opt*
    find ${output_path} -type f | grep -v "rcgu" | xargs rm
    return 
fi

echo "Compiling LLVM IR to executable..."
find ${output_path}/ -name '*.ll' | xargs -n 1 ${llvm_path}/llc -relocation-model=pic -filetype=obj

cc \
${output_path}/*.o \
-Wl,--as-needed -L ${LIB_DIR} \
-Wl,-Bstatic ${RUSTLIBS} \
-Wl,-Bdynamic -lgcc_s -lutil -lrt -lpthread -lm -ldl -lc \
-Wl,--eh-frame-hdr \
-Wl,-z,noexecstack \
-L ${LIB_DIR} \
-o ${output_path}/program \
-Wl,--gc-sections -pie \
-Wl,-z,relro,-z,now \
-nodefaultlibs \
${RUSTLIBS}
