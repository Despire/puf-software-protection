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


if [ $# -eq 0 ]; then
    echo "Generating LLMV IR..."
    RUSTFLAGS="-C save-temps --emit=llvm-bc" cargo build --release
    rm ${output_path}/*no-opt*
    find ${output_path} -type f | grep -v "rcgu" | xargs rm

    find ${output_path} -type f -name "*.ll" -delete
    find ${output_path} -type f -name "*.o" -delete

    ${llvm_path}llvm-link ${output_path}/*.bc -o ${output_path}/program.bc
    find ${output_path} -type f -name '*.bc' ! -name 'program.bc' -exec rm {} +

    exit 0
fi

echo "Compiling LLVM IR to executable..."
find ${output_path}/ -name '*.bc' | xargs -n 1 ${llvm_path}/llc -relocation-model=pic -filetype=obj

${llvm_path}clang \
${output_path}/*.o \
-Wl,-T script.txt \
-Wl,--as-needed -L ${output_path} -L ${LIB_DIR} \
-Wl,-Bstatic "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libstd-31ace390010ace71.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libpanic_unwind-09d5d4e50cccab93.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libobject-9c15c6dba45981f5.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libmemchr-828407c14d9edc70.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libaddr2line-6184e2bb774c9fa5.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libgimli-53e7321d73141540.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/librustc_demangle-e4f418405179cb88.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libstd_detect-59a0e61ff6ddb853.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libhashbrown-d5c65de39885e660.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/librustc_std_workspace_alloc-e8bd699199b968d5.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libminiz_oxide-07fff2e844eb391b.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libadler-c50267ba78fe88a7.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libunwind-bf16d2cd5e8eebb0.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libcfg_if-27550d607f826cad.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/liblibc-2a7d1fd7f5a2ea6c.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/liballoc-dc037166210470ac.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/librustc_std_workspace_core-8e50660907d425ec.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libcore-66716436790337fa.rlib" "/root/.rustup/toolchains/1.77.1-armv7-unknown-linux-gnueabihf/lib/rustlib/armv7-unknown-linux-gnueabihf/lib/libcompiler_builtins-b0b974779984b43b.rlib" \
-Wl,-Bdynamic -lgcc_s -lutil -lrt -lpthread -lm -ldl -lc \
-Wl,--eh-frame-hdr \
-Wl,-z,noexecstack \
-L ${LIB_DIR} \
-o ${output_path}/program \
-Wl,--gc-sections -pie \
-Wl,-z,relro,-z,now \
-nodefaultlibs
