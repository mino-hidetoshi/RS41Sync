# RS41Sync
A synchronization filter for RS41 Radio Sonde receivers.

This program reads a wav formatted stream of RS41 Radio Sonde signal, detects bit boundaries and averages the signals over each bit periods. It helps decoders to decode signals correctly.

## Remarks
- Works as a filter and does not hundle files. Chunk size information in the output is not correct.
- Always assumes 8bit monoral audio input with 48k sampling rate. Use sox with options -b 8 -c 1 -r 48k -t wav in other cases.
- Automatically detects polarity and always adjusts it. DO NOT USE -i option when you use rs41ptu as a decoder.
- Synchronization ( bit boundary detection ) depends on the particular header pattern and the field header patterns of rs41. Modifications are needed if those headers are different or changed.

## Usage
- sox file.wav -b 8 -c 1 -r 48k -t wav - | ./rs41sync | ./rs41ptu --ecc2 --crc --ptu

## Compile options
Performance optimization ( -O3 ) is recommended.
