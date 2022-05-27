# RS41Sync
A synchronization filter for RS41 Radio Sonde decoders.

This program reads a wav formatted stream of RS41 Radio Sonde signal, detects bit boundaries and averages the signals over each bit period. It helps decoders to decode signals correctly.

## Remarks
- Works as a filter and does not hundle files. Though a wav header is included in the output, data size information in it is not correct.
- Always assumes 8bit monoral audio input with 48k sampling rate. Use `sox` with options `-b 8 -c 1 -r 48k -t wav` in other cases.
- Automatically detects polarity and always adjusts it. DO NOT USE `-i` option when you use [rs41ptu](https://github.com/rs1729/RS/tree/master/rs41) as a decoder.
- Synchronization ( bit boundary detection ) depends on the particular header pattern and the field header patterns of rs41. Modifications are needed if those headers are changed.

## Compiling
- `gcc -O3 -o rs41sync rs41sync.c`

## Usage
- `sox file.wav -b 8 -c 1 -r 48k -t wav - | ./rs41sync | ./rs41ptu --ecc2 --crc --ptu`

## Thanks to
- [rs1729](https://github.com/rs1729) for creating [rs41ptu](https://github.com/rs1729/RS/tree/master/rs41) decoder.
- [bazingaJojo](https://github.com/bazjo) for the helpful documents [RS41 Decoding](https://github.com/bazjo/RS41_Decoding).
