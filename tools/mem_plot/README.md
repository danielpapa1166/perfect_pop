# Linker memory plot

Plot the regions declared in a GNU ld linker script's `MEMORY` block. Adjacent
regions share an address axis; separate panels omit the large gaps between
flash, internal RAM, and external SDRAM. Each panel has its own linear scale.
The script prints start addresses, inclusive last-byte addresses, and sizes.
Overlapping region definitions are hatched and reported in the terminal.

From the project root:

```bash
python3 -m pip install -r tools/mem_plot/requirements.txt
python3 tools/mem_plot/plot_memory.py
```

By default it reads `STM32F769NIHX_FLASH.ld`, saves
`tools/mem_plot/STM32F769NIHX_FLASH_memory.png`, and opens a plot window. Default
paths are relative to the script location, so it also works from another
working directory.

To save without opening a window, or select another script/output format:

```bash
python3 tools/mem_plot/plot_memory.py --no-show
python3 tools/mem_plot/plot_memory.py STM32F769NIHX_RAM.ld --no-show -o /tmp/ram_memory.svg
```

The parser supports optional region attributes, `ORIGIN`/`org`/`o`,
`LENGTH`/`len`/`l`, decimal/hexadecimal constants, `K`/`M` suffixes (powers of
1024), and numeric arithmetic/bitwise expressions. It ignores comments and
rejects unsupported expressions. It does not expand `INCLUDE`, preprocess
scripts, or resolve linker symbols/functions.

This plots **configured region capacity**, not section allocations or free
memory. For example, `.bss` overflowing the `RAM` region is diagnosed using the
linker output or `.map` file; it is not visible from the `MEMORY` block alone.

Syntax reference: [GNU ld MEMORY command](https://sourceware.org/binutils/docs/ld/MEMORY.html).
