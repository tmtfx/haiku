# XCLK post-divider decode (`PLL_XCLK_CNTL`, `Mach64_InitDSPParams()`)

`Mach64_InitDSPParams()` in `mach64_init.cpp` reads the low 3 bits of the
`PLL_XCLK_CNTL` PLL register (`mach64.h`, offset `0x0B`) into
`params.xClkPostDivider`, and decodes that 3-bit code (0-7) into two
fields used later by `SetDSPRegisters()` (`mach64_mode.cpp`) to compute
display FIFO/RAS timing:

- `xClkRefDivider` — a plain multiplier.
- `xClkPostDivider` — a power-of-2 shift amount.

i.e. `real XCLK divisor = xClkRefDivider * 2^xClkPostDivider`.

## Before this fix

The switch only handled register codes 0-4:

```cpp
switch (params.xClkPostDivider) {
case 0: case 1: case 2: case 3:
	params.xClkRefDivider = 1;
	break;          // divisor = 1,2,4,8 for codes 0,1,2,3

case 4:
	params.xClkRefDivider = 3;
	params.xClkPostDivider = 0;
	break;          // divisor = 3

default:
	TRACE("Unsupported XCLK source:  %d.\n", params.xClkPostDivider);
	return false;   // InitAccelerant() fails
}
```

Codes 5, 6, 7 fell into `default:` and were treated as unsupported
hardware, making `InitAccelerant()` return `false` and the accelerant
fail to load entirely.

## The bug this caused

On a real ATI 3D Rage XL PCI card (chip revision 0x27), `PLL_XCLK_CNTL`
reads back code `5` whenever the card is NOT the one POSTed by
firmware (i.e. it's a secondary card, with another GPU as boot
primary). `InitAccelerant()` failed outright, so the card's accelerant
never loaded and it never became a usable screen in a multi-GPU setup
— even though `device_open()` had already succeeded.

Code 5 is not "unsupported hardware": it's a real, valid register value
that this switch statement never had a case for, because on single-GPU
systems this card is always the firmware-POSTed primary, whose own
video BIOS always programs `PLL_XCLK_CNTL` to one of the codes 0-4
during POST. A secondary card, never POSTed by firmware, is left at
its raw post-reset register value instead — which is `5` on this chip.

## The fix

Verified against an external, authoritative source before touching any
more register-decode logic: Linux's fbdev `mach64_ct.c` driver
(`drivers/video/fbdev/aty/mach64_ct.c`) decodes the same 8 register
codes via:

```c
const u8 aty_postdividers[8] = {1, 2, 4, 8, 3, 5, 6, 12};
```

So codes 5/6/7 are real divisors 5, 6, 12 — not unsupported hardware.

Decomposing those three divisors into this driver's own
`xClkRefDivider * 2^xClkPostDivider` scheme (the same scheme the
existing code 4 case already uses for divisor 3 = `3 * 2^0`), with no
ambiguity:

| register code | real divisor | `xClkRefDivider` | `xClkPostDivider` |
|---------------|--------------|------------------|--------------------|
| 5             | 5            | 5                | 0                  |
| 6             | 6            | 3                | 1                  |
| 7             | 12           | 3                | 2                  |

Added as three new `case` branches alongside the existing 0-4 ones;
`default:` is kept as an unreachable safety net (the value is already
masked to 0-7 before the switch).
