# llvm-puf-passes
LLVM trasnsformation passes for patching a program to make use of a PUF. 

# How To Build

# OS X
Install LLVM via homebrew

```bash
brew install llvm@17
```

Set the directory in the CMakeLists.txt to the path of the installation of LLVM.

```CMake
set(LT_LLVM_INSTALL_DIR "/opt/homebrew/opt/llvm@17")
```

```bash
$ cmake .
$ make
```

The libraries should now be present in the `lib` folder of the project

# Ubuntu

Install LLVM-17 via apt

```bash
sudo apt install llvm-17
```

Set the installation directory for LLVM in the CMakeLists.txt

```CMake
set(LT_LLVM_INSTALL_DIR "/usr/lib/llvm-17")
```

In the root directory compile the project

```bash
cmake .
make 
```

The libraries should now be present in the `lib` folder of the project

# Use
The libraries can be used with the `opt` tool that comes installed with LLVM. Examples of using the transformation passes can be seen in the `input` directory where each example has a `compile.sh`
that uses that specific transformation.

update the `env-vars` (for example in the case of the ubuntu installation above)
```bash
#!/bin/bash

export llvm_path="/usr/lib/llvm-17"
export llvm_interpreter="/usr/lib/llvm-17/bin/lli"
export llvm_linker="/usr/lib/llvm-17/bin/llc"
```
