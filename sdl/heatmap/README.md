# Memory Heatmap Visualization

This feature provides a real-time visualization of memory access patterns in the ZX Spectrum emulator.

## Features

- Real-time visualization of memory reads and writes
- Multiple visualization modes (Read-only, Write-only, Read/Write)
- Different color schemes (Heat, Spectrum colors, Grayscale)
- Adjustable opacity
- Memory access decay for better visualization of hotspots

## Usage

Run the emulator with the `--heatmap` flag:

```bash
./build/Debug/sdl/specbolt-sdl --heatmap <snapshot>
```

## Keyboard Controls

- **F2**: Toggle heatmap on/off
- **F3**: Cycle through visualization modes (Read/Write, Read-only, Write-only)
- **F4**: Cycle through color schemes
- **F5**: Increase opacity
- **F6**: Decrease opacity
- **F7**: Reset heatmap data

## How it Works

The heatmap visualizer instruments all memory accesses in the emulator, counting reads and writes to each memory location. These counts are visualized as a transparent overlay on top of the emulator display, with color intensity representing the frequency of access.

This visualization provides insights into how programs utilize memory, revealing patterns of code execution, data access, and memory usage that would otherwise be invisible. It's particularly useful for understanding ROM routines, program behavior, and optimizing code.

## Technical Details

The implementation non-invasively hooks into the Memory class's read/write methods through a callback system, ensuring minimal performance impact while providing rich visualization data. The visualization maps the entire 64K memory space to a 256x256 pixel overlay that is blended with the main display.
