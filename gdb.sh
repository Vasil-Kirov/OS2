#!/bin/bash


gdb bin/VOS.bin -ex "target remote :1234" -ex "set disassembly-flavor intel"

