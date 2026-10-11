# Test titles

`sharpemu-demo/` is SharpEmu's open-source Breakout homebrew
([sharpemu/sharpemu-demo](https://github.com/sharpemu/sharpemu-demo) at
`49e6b3b`, GPL-2.0), built with ps5-payload-sdk v0.43 and clang 18. It is a
plain PS5 ELF that needs no data files and plays itself when idle, which makes
it a legal smoke test and a simple benchmark:

```powershell
.\_Build\windows\install\kyty_emulator.exe --game tools\bench\testdata\sharpemu-demo
python tools\bench\kyty_bench.py --emu _Build\windows\install\kyty_emulator.exe `
    --game tools\bench\testdata\sharpemu-demo --name "Breakout demo" --warmup 5 --seconds 30
```

To rebuild it: `PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk python3 build.py <out>`
from a checkout of the source repo.
