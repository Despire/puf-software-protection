### Alloc Sysram
This kernel module will allocate a continuous chunk of memory on the Beagle Bone Black.
A memory chunk up to 8MB can be allocated. (which is more than enough for a PUF).

when loading the kernel module, it is possible to specify the chunk of the memory to be allocated via the params
The following command will allocate a chunk of 4MB.

`insmod alloc_sysram.ko alloc_size_1=4194304`

The memory allocated by the kernel module can be dumped by using `cat /dev/puf_block_1`

The address of the physical memory will be dumped, and one can receive it by using the `dmesg` command.

```bash
[   60.976616] Allocated 4194304 bytes of physically contiguous memory
[   60.976640] Virtual address: c4c00000
[   60.976647] Physical address: 84c00000
```

### Plain Puf

This kernel module should only be using during the enrollments of the DRAM cells.
It implements the minimum PUF implementation that when loaded (insmod) zeroes the 
memory and starts the decaying of the cells. When unloaded (rmmod) will stop the decaying of cells
and one can dump the decayed cells using the `cat /dev/puf_block_1` command mentioned above.

To load the kernel module you would execute the following command (given the address and size
of the memory region from the above alloc sysram kernel module).

`insmod dram_puf.ko puf_block_1=0x84c00000 puf_block_1_size=4194304`

After loading in `dmesg` you would see the exact same output as with the `alloc_sysram` module.

```bash
[   68.939776] PUF virtual address: c4c00000
[   68.939800] PUF physical address: 84c00000
```

### PUF

This kernel module should only be used with a patched binary. You can load this kernel module by

`insmod dram_puf.ko puf_phys_addr=0x84c00000 puf_size=4194304`

After the kernel module is running, and the warm up phase (10 mins) of the PUF has finished which, the patched binary can use the kernel module.

This kernel module will:

1. Open a device under `/dev/puf`
    The first write to the device is expected to be the enrollments. 
    any write resets the PUF.
    any read uses the enrollments to read the requested DRAM cells.
2. Only a single process can open the `dev/puf` at a time.
3. On load it has a "warm up" period of 10 mins after wich it can be used any number of times until unloaded.

An example output of the kernel module from `dmesg` after a reconstructed PUF response.

```bash
[ 4892.771474] puf opened by process with PID: 1247
[ 4892.771498] PUF waiting for enrollment data
[ 4892.771543] recieved byte count: 644
[ 4892.771550] Data written to device:
[ 4892.771555]  10 00 18 00 90 00 6b 00 a6 00 cc 00 03 00 1e 00 7d 00 d5 00 23 00 1f 00 52 00 1f 00 57 00 25 00 3e 01 40 a5 a7 01 82 e0 ff 01 e7 bf 7d 00 64 6d ad 00 b1 88 fd 01 87 1a fd 01 5e 99 fc 01 68 ec f2 01 b4 82 60 01 d2 ab a2 01 41 bd 62 01 85 c9 f4 00 73 8d eb 01 b8 96 fa 01 05 7a 3d 00 31 8c b9 00 74 3d f7 00 35 30 eb 00 cf fe fd 00 7c 2a e2 00 af 56 a6 01 2d 50 be 00 03 cd b8 00 e3 04 f6 00 93 96 bf 01 58 55 20 01 d3 82 ee 01 dc a4 ed 00 19 c4 f0 01 9b 54 b3 00 b0 e1 fe 00 92 46 20 10 00 a7 00 f3 00 5d 00 7b 00 ac 00 84 00 55 00 54 00 ce 00 cf 00 67 00 b6 00 49 00 36 00 e8 00 1c 00 f5 d3 be 01 5b bd fc 00 61 95 af 00 d6 60 3b 01 3d 34 21 01 ac 9f 2a 01 d8 1e 65 01 c3 41 b7 01 e5 ab 34 00 27 6e 62 01 a8 ba a9 00 02 1e 21 01 e5 b2 78 01 5c ae f3 00 04 a0 6b 01 a0 29 be 00 ba 03 37 01 75 bf 37 01 89 3d ea 01 9e ab 22 01 ea 4b fe 00 8c 00 e9 01 ac f8 2c 01 3e b2 38 00 60 7c 3a 00 a1 a1 6c 01 ad 03 2f 01 d5 77 79 01 77 ab f2 01 13 18 64 00 56 55 b6 00 7f c5 b7 10 00 f0 00 09 00 52 00
[ 4892.773039]  bb 00 eb 00 b2 00 52 00 c2 00 fb 00 fb 00 42 00 e2 00 0e 00 84 00 df 00 2c 01 f8 f0 f9 01 44 16 38 01 c9 b4 6b 00 86 f9 f2 00 11 63 28 01 a6 1a 60 01 21 3a ef 01 ab a0 fa 00 8d 27 d5 01 a1 f4 69 01 00 3b 94 01 5f 81 37 01 aa fa 85 01 03 84 ff 00 a7 ee 73 01 1b e8 6b 00 84 1e 60 00 9e 2e b6 00 2f d6 75 01 ff 13 ad 00 43 3c 37 00 ad 31 4c 01 8e ab a2 00 bb 0d e0 01 62 b2 50 00 8b 49 af 00 61 dc ef 01 10 f5 39 00 c9 42 6b 00 c8 96 f3 01 0f c6 ae 00 b5 92 33 10 00 44 00 30 00 78 00 e8 00 9f 00 62 00 0c 00 b0 00 16 00 81 00 48 00 c8 00 fc 00 58 00 d3 00 c5 01 cd a1 b4 00 9e b2 f6 00 b7 7d cc 01 74 bd 36 00 f4 0d fa 00 03 a9 43 00 66 f7 15 00 4c 93 6e 00 a9 68 89 01 dc 45 fa 01 e5 8d 64 01 36 a0 ee 00 93 ba aa 00 2e 3b 63 01 d7 28 d2 00 22 5e e6 01 5a ff 31 00 32 b6 e5 00 b4 42 f9 00 4e 5a 9c 00 92 85 24 00 97 d4 7e 00 9a 4d f7 00 83 a8 da 00 2a 1c 14 01 1d 12 e7 01 18 f0 77 01 38 cc f9 00 0a 11 4d 00 5d e9 39 01 36 f7 b0 01 cf 48 fe
[ 4892.774448] received enrollment data
[ 4892.788195] DISABLE, RCTRL=0x00000c30
[ 4892.788218] DISABLE, RCTRL_SDHW=0x00000c30
[ 4892.788226] DISABLE, RCTRL=0x80000c30
[ 4892.788232] DISABLE, RCTRL_SDHW=0x80000c30
[ 4917.802725] debug: parity[0]:24
[ 4917.802748] debug: parity[1]:144
[ 4917.802756] debug: parity[2]:107
[ 4917.802764] debug: parity[3]:166
[ 4917.802771] debug: parity[4]:204
[ 4917.802777] debug: parity[5]:3
[ 4917.802784] debug: parity[6]:30
[ 4917.802790] debug: parity[7]:125
[ 4917.802797] debug: parity[8]:213
[ 4917.802803] debug: parity[9]:35
[ 4917.802810] debug: parity[10]:31
[ 4917.802816] debug: parity[11]:82
[ 4917.802822] debug: parity[12]:31
[ 4917.802829] debug: parity[13]:87
[ 4917.802836] debug: parity[14]:37
[ 4917.802842] debug: parity[15]:62
[ 4917.802852] debug: block:1313370 mask: 7 ptr: 21013927 memoffset: 2626740 value at address:0
[ 4917.802872] debug: block:1584655 mask: 15 ptr: 25354495 memoffset: 3169310 value at address:0
[ 4917.802886] debug: block:1997815 mask: 13 ptr: 31965053 memoffset: 3995630 value at address:0
[ 4917.802899] debug: block:411354 mask: 13 ptr: 6581677 memoffset: 822708 value at address:0
[ 4917.802912] debug: block:727183 mask: 13 ptr: 11634941 memoffset: 1454366 value at address:0
[ 4917.802927] debug: block:1601967 mask: 13 ptr: 25631485 memoffset: 3203934 value at address:0
[ 4917.802940] debug: block:1436063 mask: 12 ptr: 22977020 memoffset: 2872126 value at address:0
[ 4917.802953] debug: block:1478351 mask: 2 ptr: 23653618 memoffset: 2956702 value at address:0
[ 4917.802967] debug: block:1787942 mask: 0 ptr: 28607072 memoffset: 3575884 value at address:1
[ 4917.802980] debug: block:1911482 mask: 2 ptr: 30583714 memoffset: 3822964 value at address:4
[ 4917.802994] debug: block:1317846 mask: 2 ptr: 21085538 memoffset: 2635692 value at address:4
[ 4917.803007] debug: block:1596575 mask: 4 ptr: 25545204 memoffset: 3193150 value at address:16
[ 4917.803021] debug: block:473310 mask: 11 ptr: 7572971 memoffset: 946620 value at address:2048
[ 4917.803035] debug: block:1804655 mask: 10 ptr: 28874490 memoffset: 3609310 value at address:0
[ 4917.803048] debug: block:1071011 mask: 13 ptr: 17136189 memoffset: 2142022 value at address:8192
[ 4917.803062] debug: block:202955 mask: 9 ptr: 3247289 memoffset: 405910 value at address:0
[ 4917.803075] debug: block:476127 mask: 7 ptr: 7618039 memoffset: 952254 value at address:0
[ 4917.803089] debug: block:217870 mask: 11 ptr: 3485931 memoffset: 435740 value at address:2048
[ 4917.803102] debug: block:851951 mask: 13 ptr: 13631229 memoffset: 1703902 value at address:8192
[ 4917.803116] debug: block:508590 mask: 2 ptr: 8137442 memoffset: 1017180 value at address:4
[ 4917.803129] debug: block:718186 mask: 6 ptr: 11490982 memoffset: 1436372 value at address:64
[ 4917.803143] debug: block:1234187 mask: 14 ptr: 19747006 memoffset: 2468374 value at address:16384
[ 4917.803156] debug: block:15579 mask: 8 ptr: 249272 memoffset: 31158 value at address:256
[ 4917.803170] debug: block:929871 mask: 6 ptr: 14877942 memoffset: 1859742 value at address:0
[ 4917.803184] debug: block:604523 mask: 15 ptr: 9672383 memoffset: 1209046 value at address:0
[ 4917.803197] debug: block:1410386 mask: 0 ptr: 22566176 memoffset: 2820772 value at address:1
[ 4917.803211] debug: block:1914926 mask: 14 ptr: 30638830 memoffset: 3829852 value at address:0
[ 4917.803224] debug: block:1952334 mask: 13 ptr: 31237357 memoffset: 3904668 value at address:0
[ 4917.803238] debug: block:105551 mask: 0 ptr: 1688816 memoffset: 211102 value at address:1
[ 4917.803251] debug: block:1684811 mask: 3 ptr: 26956979 memoffset: 3369622 value at address:0
[ 4917.803265] debug: block:724511 mask: 14 ptr: 11592190 memoffset: 1449022 value at address:0
[ 4917.803278] debug: block:599138 mask: 0 ptr: 9586208 memoffset: 1198276 value at address:1
[ 4917.803290] debug: recovered bits -  00 00 00 00 00 00 00 00 01 01 01 01 01 00 01 00 00 01 01 01 01 01 01 00 00 01 00 00 01 00 00 01
[ 4917.803463] bits after ECC:  00 00 00 00 00 00 00 00 01 01 01 01 01 00 01 00 00 01 01 01 01 01 01 00 00 01 00 00 01 00 00 01
[ 4917.803620] debug: reconstructed response - 16416329(hex: 0x00fa7e49)
```
