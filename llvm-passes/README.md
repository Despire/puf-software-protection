# llvm-puf-passes

LLVM trasnsformation passes for patching a program to make use of a PUF.

# How To Build

Before building using cmake you need to correctly seth path to LLVM.

# OS X

Install LLVM via homebrew

```bash
brew install llvm@17
```

Set the directory in the CMakeLists.txt to the path of the installation of LLVM.

```CMake
set(LT_LLVM_INSTALL_DIR "/opt/homebrew/opt/llvm@17")
```

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

The libraries should now be present in the `lib` folder of the project

# Use

The libraries can be used with the `opt` tool that comes installed with LLVM. An example usage can be identified by
the `patch` in
the Makefile of the root directory of the repository.

the LLVM pass supports multiple arguments when running.
```
  - prefix
      when specified it will only consider functions with the given prefix as entry points and any
      function call within these functions will be used for call graph scrambling.
  - enrollment
      Enrollment json file that was generated.
  - outputjson
      Will output which functions will be considered for patching. no actual patching is done.
  - inputjson
      Will patch the IR based from the information of the compiled binary.
  - checksum-count
      number of checksum call performed per function.
```
