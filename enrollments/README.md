### Scripts

Contains the script to perform enrollments

Assumes that the `alloc_sysram` kernel module was loaded. Set the PUF start and size based on the
values from the `alloc_sysram` output in `dmesg`.

The script assumes that the `kmod/plain_puf` is compiled on the Beagle Bone and the `dram_puf.ko` can
be found under `/home/debian/puf/dram_puf.ko`.

### JSON

The `bbb_config.json` is a configuration file used by the `enroll` command line app to generate
the enrolled DRAM cells. It has default values (which are configurable) and reflect the DRAM
cells enroll in the `enroll_bbb.zip`.

### Enroll

Is a command line app that takes a configuration file and outputs a JSON file with the enrolled
DRAM cells. This JSON file is then further used by the LLVM pass.

