#ifndef LLVM_OBFUSCATOR_UTILS_H
#define LLVM_OBFUSCATOR_UTILS_H

#define LLVM_I32(ctx)       llvm::IntegerType::getInt32Ty(ctx)
#define LLVM_CONST_I32(ctx, val) llvm::ConstantInt::get(LLVM_I32(ctx), val)

// Custom return value when the device fails to open.
#define DEV_FAIL 0x9c

#endif //LLVM_OBFUSCATOR_UTILS_H
