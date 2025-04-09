#!/usr/bin/env python3
# Add boot signature (one page of zeros) at gived address.
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import sys, argparse, struct

def renumerate(content):
    result = bytearray()
    total = len(content) // 512
    for current in range(total):
       block = content[current * 512 : (current + 1) * 512]
       result.extend(block[ : 20])
       result.extend(struct.pack("<II", current, total))
       result.extend(block[ 28 : ])
    return result

def add_signature(content, address):
    block = content[:512]
    nblock = bytearray()
    nblock.extend(block[:12])
    nblock.extend(struct.pack("<I", address))
    nblock.extend(block[16:32])
    nblock.extend(bytearray(476))
    nblock.extend(block[508:512])
    result = bytearray()
    result.extend(content)
    result.extend(nblock)
    return result

def main():
    parser = argparse.ArgumentParser(description="Merge multiple uf2")
    parser.add_argument("-o", "--output", required=True, help="Output file")
    parser.add_argument("-i", "--input",  required=True, help="Input files")
    parser.add_argument("-a", "--address",  required=True, help="Address to put signature")

    args = parser.parse_args()
    address = int(args.address,0)
    with open(args.input, 'rb') as f:
       content = f.read()
    content = add_signature(content, address)
    content = renumerate(content)
    with open(args.output, 'wb') as f:
       f.write(content)

if __name__ == '__main__':
    main()
