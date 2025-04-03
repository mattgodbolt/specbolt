# SpecBolt Project Glossary

This document defines terminology specific to the ZX Spectrum, emulation, and the SpecBolt project to ensure consistent understanding across the codebase.

## ZX Spectrum Hardware

### CPU and Memory

| Term | Definition |
|------|------------|
| Z80 | The Zilog Z80 CPU used in the ZX Spectrum, an 8-bit microprocessor. |
| ULA | Uncommitted Logic Array - The custom chip in the ZX Spectrum that handles video generation, keyboard scanning, and sound. |
| ROM | The 16KB read-only memory containing the Spectrum's BASIC interpreter and operating system. |
| RAM | Random Access Memory - The 48KB or 128KB writable memory used for programs and data. |
| I/O Ports | Special memory-mapped ports used to communicate with hardware devices. |
| Contended Memory | Memory addresses that are subject to timing delays due to the ULA sharing access with the CPU. |

### Video

| Term | Definition |
|------|------------|
| Attribute | A byte controlling the foreground and background colors of an 8x8 pixel cell. |
| Character Cell | An 8x8 pixel area with the same attribute settings. |
| Display File | The area of memory (0x4000-0x5AFF) that holds the screen data. |
| Attribute File | The area of memory (0x5800-0x5AFF) that holds the color attributes for the screen. |
| Border | The color area surrounding the main screen display. |
| BRIGHT | A flag in the attribute byte that increases the intensity of colors. |
| FLASH | A flag in the attribute byte that alternates foreground and background colors. |
| INK | Foreground color (the color of pixels set to 1). |
| PAPER | Background color (the color of pixels set to 0). |

### Audio

| Term | Definition |
|------|------------|
| Beeper | The internal speaker system of the ZX Spectrum. |
| AY-3-8912 | The sound chip found in 128K Spectrum models, offering 3-channel sound. |

### Input/Output

| Term | Definition |
|------|------------|
| Keyboard Matrix | The grid arrangement of the ZX Spectrum keyboard. |
| Tape Interface | The cassette tape interface for loading and saving programs. |
| Interface 1 | Add-on providing RS-232, network, and Microdrive connections. |
| Interface 2 | Add-on for ROM cartridges and joystick ports. |

## File Formats

| Term | Definition |
|------|------------|
| .z80 | A snapshot file format capturing the entire state of the Spectrum's memory and registers. |
| .sna | Alternative snapshot format, similar to .z80 but with a simpler structure. |
| .tap | Tape file format that emulates data stored on cassette tapes. |
| .tzx | Advanced tape file format supporting various loading schemes and copy protections. |

## SpecBolt Architecture

| Term | Definition |
|------|------------|
| Listener | Design pattern used for components that need to observe and react to events. |
| Memory::Listener | Interface for components that need to track memory access patterns. |
| Heatmap | Visualization that shows memory access patterns with color coding. |
| Frontend | The user interface implementation (SDL, Console, Web). |
| Disassembler | Component that converts machine code bytes back to assembly language. |
| Snapshot | An image of the machine's memory and CPU state, used for save states. |
| Peripheral | Hardware components connected to the main system (Keyboard, Video, Audio, etc.). |

## SpecBolt Implementation

| Term | Definition |
|------|------------|
| Z80 Version | Different implementations of the Z80 emulation (v1, v2, v3) with varying approaches. |
| Memory Address Space | The complete 64KB memory map that the Z80 can access. |
| C++ Module | A feature introduced in C++20 for organizing code, used as an alternative to header files. |
| Register File | Collection of CPU registers (A, F, BC, DE, HL, etc.). |
| Decoder | Component that interprets Z80 opcodes and maps them to operations. |
| Scheduler | Component managing the timing and execution of CPU and peripherals. |

## Development Terms

| Term | Definition |
|------|------------|
| SPECBOLT_MODULES | CMake flag that enables or disables the use of C++ modules. |
| WASI | WebAssembly System Interface - Used for the web version of SpecBolt. |
| SDL | Simple DirectMedia Layer - Library used for graphics, input, and audio. |
| PascalCase | Naming convention where compound words start with capital letters (e.g., MemoryHeatmap). |
| snake_case | Naming convention where words are lowercase and separated by underscores (e.g., memory_access). |

This glossary will be updated as new terms and concepts are added to the project.
