#/bin/bash

mkdir -p "${output_path}"

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

rustc -C save-temps -C opt-level=3 --emit=llvm-ir ${input_path}.rs -o ${output_path}/ex1

rm ${output_path}/*no-opt*
find ${output_path} -type f | grep -v "rcgu" | xargs rm

find ${output_path}/ -name '*.ll' | while read -r file; do
  ${llvm_path}/bin/opt -load-pass-plugin ./cmake-build-debug/lib/libChecksum.dylib -passes=checksum -S ${file} -o ${file}
done

find ${output_path}/ -name '*.ll' | xargs -n 1 ${llvm_path}/bin/llvm-as
find ${output_path}/ -name '*.bc' | xargs -n 1 ${llvm_path}/bin/llc -relocation-model=pic -filetype=obj

cc \
${output_path}/*.o \
-o ${output_path}/program \
-lSystem \
-L ${LIB_DIR} \
-nodefaultlibs \
${RUSTLIBS}
