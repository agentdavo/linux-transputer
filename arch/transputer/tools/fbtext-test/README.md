# Framebuffer glyph validation

The bare-metal test links the actual kernel `fbtext.o` and compares its
output with a separate renderer that evaluates individual font bits.
It expects the SPINE configuration with an 800 by 600 XRGB framebuffer at
`0x81000000`. Reference pixels use scratch memory at `0x81400000`.

The test sends all 256 byte inputs, 4,107 printable characters, and forty
groups of newline, carriage return, invalid characters and printable text.
It checks every pixel after each phase, including cursor wraparound,
untouched scanlines and adjacent memory canaries. It copies the final
framebuffer to T4's capture window so the emulator can save a BMP.

From a TinyCC Transputer workspace, build and run with its toolchain and a
kernel build directory containing the configured `fbtext.o`.

```sh
bash transputer/linux-7/arch/transputer/tools/fbtext-test/build.sh \
    "$PWD" /path/to/kernel-build /path/to/test-output
cd /path/to/test-output
T4_HEADLESS=1 T4_UART_LOG=uart \
    /path/to/t4 -s9 -sm 25 -ss -sb test.btl
```

`TCC_BIN` and `NETLOAD_BIN` may select frozen tool binaries. The test prints
`GLYPHS PASS`, followed by the bootstrap's `H` marker. The three pixel hashes
are `2cdf0dc5`, `f9848145`, and `8347d445`.

## Measurement on 17 November 2026

A single lookup of a font byte replaces the two nibble lookups for each
nonblank glyph row. The blank-row stores retain their fast path.

| Fixed stream, glyph routine | Baseline | Byte table | Reduction |
| --- | ---: | ---: | ---: |
| Decoded instructions | 4,180,300 | 3,728,239 | 10.8% |
| Modeled T800 cycles | 8,794,737 | 7,877,212 | 10.4% |
| Workspace loads, stores and address loads | 1,912,916 | 1,702,549 | 11.0% |

Both kernel objects pass the pixel oracle, and their captured BMPs match
byte for byte with SHA-256
`a37fb82fc6b075689ade9237470f1b906a6723b498edd19075b7430147e78c45`.
The lookup table grows from 256 to 8,192 bytes. The linked BTL grows by
8,224 bytes.

The rebuilt Linux image passes the protection self-test and reaches
`DESKTOP_LOOP` under T4 `-s806 -sm 25` with hard-P enabled. Its strict link
has zero undefined symbols and the call audit checks 81,427 relocations with
zero mismatches. The three existing modpost section-mismatch warnings persist.
The actual SPINE framebuffer was captured from the task-owned emulator's
RAM at desktop entry. Baseline and final 800 by 600 XRGB buffers are
byte-identical with SHA-256
`0859bf0f8ef7b7fa269f0d67644dbb77f6f57f5fa8aca5967ff782c9c1ac616e`.
The boot profile records 4,000,269 glyph instructions, compared with
4,423,005 in the baseline. Timing-related console text varies between boots,
so the fixed stream above supplies the paired performance comparison.

Final BTL SHA-256 is
`1979cb56faac295ef3a08da1a692cbe2097a2662ab29aa0016e60b0e5fa4f6ed`.
The compiler binary is the retained TinyCC `f2ac20b4` validation build.

## Whole-space glyph fast path

The space glyph now clears its eight by sixteen rectangle directly,
without loading font bytes or testing each scanline. Invalid input bytes
still map to that same space glyph.

| Fixed stream, glyph routine | Byte table | Space fast path | Reduction |
| --- | ---: | ---: | ---: |
| Decoded instructions | 3,728,239 | 3,696,179 | 0.86% |
| Modeled T800 cycles | 7,877,212 | 7,823,790 | 0.68% |

The pixel oracle passes with the same three hashes and captured BMP hash
above. The rebuilt BTL reaches `DESKTOP_LOOP` with hard-P enabled and the
protection self-test passing. Its actual SPINE framebuffer is byte-identical
to the previous desktop capture. The boot profile records 3,928,540 glyph
instructions; use the fixed stream for the paired performance comparison.
The strict linker again checks 81,427 call relocations with zero mismatches.

Space-fast-path BTL SHA-256 is
`305ccfac2049021c2dfd223ae63ab53c910c12fdfbc534db2a86a5a7a81fc93f`.
