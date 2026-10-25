# Perfect Pop

## Motivation 

Have you ever burnt popcorn? Or stopped the microwave too early and had a bunch of unpopped kernels?

Well, I have. 

One evening, while preparing popcorn for a movie night with my wife, I started counting the pops and noticed how the popping rate changed over time.

This gives us a histogram of the pops over time. 

// plot of a Bell curve 

Suddenly an idea came to mind: 
What if this characteristic is always the same?

You can change the microwave, the power, the brand of the popcorn, but the shape of this curve remains. 

// plot of the effect of different microwave power setting, and number of kernels in the bag

The higher the microwave power, the faster the kernels will pop. But if you leave everything else unchanged, the curve gets squeezed, but the shape remains the same. Also the area under the curve (i.e. the total number of kernels in the bag) will be the same. 

If this is true (to be proven...), and we count the pops during the making of the popcorn, then we can plot the histogram and fit a curve to the observed popping rate, and predict how many more kernels might pop. 

// plot of the Bell curve fit

With this the optimal heating time can be calculated without knowing the number of kernels, or even the power of the microwave. 

Could the changing popping rate help predict when to stop heating? My hypothesis is that different batches follow a sufficiently similar pattern to make this possible. The plan is to detect individual pops, estimate their rate over time, and test whether fitting a model can predict the remaining popping activity without knowing the batch size or microwave power in advance.

## Architecture 

I found an STM32 F769 Discovery board in my drawer. It seemed suitable, as it has microphones to capture the pops, and a screen to display the statistics, or maybe plot the histogram. Also in the last few months I only worked on POSIX systems (mainly QNX, embedded linux), so I kind of felt glad to drop back to bare metal. 

### HW 

STM32 F769 Disco board ... todo ... 

### System architecture 

// todo ... 


## Why this project?  

Is this complete over-engineering? 
Yes. 
Is this more engineering than movie night strictly requires? 
Yes. 
Am I learning something with this project? 
Yes. 
Am I having fun? 
Absolutely.

## LVGL setup in STM32CubeIDE

`Middlewares/lv_conf.h` is the project's LVGL configuration. It is copied from
`Middlewares/lvgl/lv_conf_template.h`, with the first `#if 0` changed to `#if 1`.
Keep it next to the `lvgl` directory: LVGL's default relative include resolves
to this location. Do not edit the generated `lv_conf_internal.h`.

The display uses `LV_COLOR_FORMAT_ARGB8888` to match LTDC's input format.
Examples and demos are disabled. The display task initializes LVGL, registers
`HAL_GetTick`, and calls `lv_timer_handler()` periodically.

LVGL renders directly into one 800 x 480 x 4-byte framebuffer shared with LTDC
at `0xC0000000` in external SDRAM (1,536,000 bytes / 1,500 KiB). The flash
linker script reserves `.sdram_framebuffer` as a 32-byte-aligned `NOLOAD`
section; `display_init()` clears it after `MX_FMC_Init()` has initialized SDRAM.
The existing non-cacheable 2 MiB MPU region covers the entire framebuffer.
Linker assertions check the base address and that the buffer fits this region.

The path is LVGL -> SDRAM framebuffer -> LTDC -> DSI -> panel, using
`LV_DISPLAY_RENDER_MODE_DIRECT` without a pixel copy in the flush callback.
Automatic DSI refresh is disabled by the display driver. Once the last dirty
area has been drawn, the flush callback starts a manual full-panel transfer
and polls end-of-refresh, yielding the display task between checks. It calls
`lv_display_flush_ready()` only after that transfer completes, so LVGL cannot
modify the single buffer while LTDC/DSI reads it. No DSI IRQ handler is needed.
This is a single-buffer implementation; panel TE synchronization and parallel
rendering with a second framebuffer are not configured.

If a transfer fails or exceeds 100 ms, the driver stops in `Error_Handler()`.
Inspect `s_panel_debug.refresh_status` and `s_panel_debug.refresh_count` in GDB
to distinguish refresh failures from RTOS stack or allocation failures.

The built-in LVGL allocator keeps its 64 KiB pool in `.lvgl_pool`, using
`LV_ATTRIBUTE_LARGE_RAM_ARRAY` in `Middlewares/lv_conf.h`. The active flash
linker script, `STM32F769NIHX_FLASH.ld`, places this section in `RAM_D2`
(SRAM1/SRAM2), after `.dma_buffer`, with 8-byte alignment. It is `NOLOAD`:
`lv_mem_init()`, called by `lv_init()`, initializes the allocator metadata
without requiring startup zeroing of the pool. Check `Debug/perfect_pop.map`
to inspect the internal RAM sections and the separate SDRAM framebuffer.

The display task has an 8 KiB stack; the Debug build's first label-rendering
call chain requires about 4 KiB before exception/context overhead. FreeRTOS has
a separate 24 KiB heap to cover task stacks, task control blocks, and kernel
objects. These sizes are also saved in `perfect_pop.ioc`.

Inspect `g_display_stack_min_free_bytes` in GDB after rendering to see the
display task's minimum unused stack in bytes. Stack-overflow checking (method
2) and the allocation-failure hook are enabled. They halt in
`vApplicationStackOverflowHook()` or `vApplicationMallocFailedHook()`;
`g_rtos_failure_reason` identifies stack overflow (1) or FreeRTOS heap
exhaustion (2), and `g_rtos_failed_task` identifies the overflowing task.
Overflow checks run at context switches and cannot catch every overwrite
before corruption occurs. `HAL_GetTick` remains the LVGL tick callback: TIM6
maintains its millisecond counter independently of the FreeRTOS SysTick.

Under **Project Properties > C/C++ Build > Settings > MCU/MPU GCC Compiler >
Include paths**, add these paths for each build configuration:

```text
"${workspace_loc:/${ProjName}/Middlewares/lvgl}"
"${workspace_loc:/${ProjName}/Middlewares/lvgl/include}"
"${workspace_loc:/${ProjName}/Middlewares/lvgl/include/lvgl}"
```

The root path supports `#include "lvgl.h"`; the other paths support the headers
in this LVGL checkout. Apply the same paths to any folder with its own compiler
settings, particularly `Core`. Assembler paths alone do not affect C compilation.

Compile the LVGL sources under `Middlewares/lvgl/src`. Use **Resource
Configurations > Exclude from Build** to exclude `tests`, `examples`, `demos`,
`env_support`, `scripts`, and the other directories outside `src` within LVGL.
Also exclude `src/libs/thorvg`, since ThorVG is disabled in this configuration.
Refresh the project and clean/rebuild after changing the source selection.

If the compiler still builds files under `lvgl/tests`, check the exclusions in
the running IDE. Disabling demos/examples in `lv_conf.h` does not exclude source
files from CubeIDE's build. In particular, the LVGL tests contain their own
`main()` functions and platform dependencies and must be excluded separately.

When `.cproject` is edited externally while CubeIDE is open, the IDE can retain
its previous settings and regenerate `Debug/makefile` with the old source list.
F5 refresh alone may not reload those settings. Close CubeIDE before applying
external project-setting changes, then reopen it and use **Project > Clean**
followed by **Build Project**. Alternatively, apply the exclusions and include
paths through the running IDE's property dialogs. Do not edit the generated
makefiles as the fix; CubeIDE will regenerate them.

CubeIDE's `.cproject` file is ignored by this repository, so these IDE settings
must be recreated when importing a fresh checkout. The current LVGL submodule
identifies itself as `10.0.0-dev`; use its bundled template and matching APIs.

The FT6X06 touchscreen driver lives in `Middlewares/stm32-ft6x06`. Keep the
application configuration at `Middlewares/ft6x06_conf.h` so the submodule stays
unchanged. Add `../Middlewares` and `../Middlewares/stm32-ft6x06` to the C compiler
include paths in Debug and Release, including the Debug `Core` folder override.
The first path exposes the configuration header; the second exposes `ft6x06.h`.
The `Middlewares` source entry includes both `ft6x06.c` and `ft6x06_reg.c` in the
build. `FT6X06_TS_Drv_t` is the driver-table type; `FT6X06_TS_Driver` is the
driver table exported by ST.
