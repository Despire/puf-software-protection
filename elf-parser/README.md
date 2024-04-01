Utility command line tool to help parse .TEXT section of ELF binaries to read
information about the compiled functions, namely offsets, sizes, names...

Has 2 commands

- read 
```
parsers the .TEXT segment of the compiled Elf binary and outputs some which functions are present in the final binary.
```
- patch 
```
   patches the .TEXT segment by looking for "MARKERS" which will be replaced to make hash function hash to 0.
   patches the .TEXT segment by looking for "MARKERS" to patch the bounds of the checksumming functions.
   patches the .TEXT segment by looking for "MARKERS" to path the refernece values used in the function address calculations.
```
