# SDKDumperUE

**Made by xSerndev** — UE SDK dumper + manual-map injector for non-anticheat games.

> ⚠️ **Works only on games without active kernel-level anti-cheat (EAC / BattlEye / Vanguard / VAC).** Attempting injection into a protected title will fail with `ACCESS_DENIED`. This is expected and by design — kernel AC hooks strip VM access from any handle you open.

---

## What it does

Injects `Dumper.dll` into a running Unreal Engine game process. The DLL walks `GObjects` + `FNamePool`, reconstructs all classes / structs / enums from reflection data, and writes per-package C++ headers to disk. Every generated file is watermarked:

```cpp
// SDKDumperUE - Made by xSerndev
```

Output goes to `%LocalAppData%\SDKDumperUE\<GameName>\`.

---

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 Community (Desktop C++ workload, Windows 10 SDK)
- Target game must be **x64** and **not EAC / BattlEye / Vanguard protected**

---

## Build

1. Open `SDKDumperUE.sln` in Visual Studio 2022
2. Set configuration to **Release | x64**
3. Build All (`Ctrl+Shift+B`)

Outputs:
```
x64\Release\Dumper.dll    <- inject this
x64\Release\Injector.exe  <- the injector
```

---

## Usage

### Drag & Drop (easiest)

Drag `Dumper.dll` onto `Injector.exe`. An interactive process picker opens automatically listing all detected UE x64 processes. Pick by number, PID, or exe name.

```
pick index, pid, exe-name, 'a' for all, 'r' to refresh, 'q' quit > 2
```

### Command Line

```bat
Injector.exe --dll "x64\Release\Dumper.dll" --proc MyGame-Win64-Shipping.exe
Injector.exe --dll "x64\Release\Dumper.dll" --pid 12345
```

The injector auto-elevates via UAC on launch. Run as admin if UAC is off.

### Picker shortcuts

| Input | Action |
|-------|--------|
| `0`–`N` | select by list index |
| any number > list size | treat as raw PID |
| exe name / partial name | search by name |
| `a` | show all x64 processes |
| `r` | refresh list |
| `q` | quit |

---

## Output

After successful injection a console appears in the game process and logs progress. Files land at:

```
%LocalAppData%\SDKDumperUE\<GameName>\
    SDK.hpp                  <- umbrella include
    Engine_classes.hpp
    CoreUObject_classes.hpp
    <PackageName>_classes.hpp
    ...
    dumper.log
```

Each header contains reconstructed enums, structs with per-field offsets, classes with inheritance chain, and function stubs with flag annotations.

---

## Compatible targets

Works on any UE 4.25+ / UE5 game that is **not** running an active kernel anti-cheat driver. Examples:

- Singleplayer UE games (no AC, or AC not active in singleplayer mode)
- UE Editor (`UnrealEditor-Win64.exe`) — great for testing
- Packaged development / debug builds
- Modding-friendly UE titles
- Older games shipped before EAC was common

---

## Supported UE versions

| Range | Status |
|-------|--------|
| UE 4.25 – 4.27 | ✅ primary target (FNamePool + FField layout) |
| UE 5.0 – 5.4 | ✅ same layout, tested |
| UE 4.0 – 4.24 | ⚠️ needs offset overrides (TNameEntryArray, UProperty instead of FField) |

Default offsets are tuned for **UE 4.27 / UE5 shipping, `WITH_CASE_PRESERVING_NAME=0`**. To support older builds, edit `Dumper\src\Engine\Offsets.h`.

---

## Extending sigs

If the dumper fails to find `GObjects` or `GNames`, the sig table is in [`Dumper/src/Engine/Sigs.cpp`](Dumper/src/Engine/Sigs.cpp). Each pattern is tried in order; add your IDA-style byte pattern as a new entry in the initializer list.

---

## Project layout

```
SDKDumperUE.sln
│
├── Dumper/
│   └── src/
│       ├── dllmain.cpp              entry point, orchestrates everything
│       ├── Watermark.h              banner string injected into every output file
│       ├── Engine/
│       │   ├── Offsets.h            all struct field offsets (tune per game)
│       │   ├── Sigs.cpp/.h          AOB sig scanner -> GObjects / GNames / AppendString
│       │   ├── NameArray.cpp/.h     FNamePool walker + AppendString fallback
│       │   ├── ObjectArray.cpp/.h   FUObjectArray chunked walker
│       │   └── UnrealTypes.h        raw layout types (FName, TArray, FUObjectArray…)
│       ├── Generator/
│       │   └── Generator.cpp/.h     walks objects, emits per-package headers
│       └── Utils/
│           ├── Logger.h             console + file logger
│           └── Memory.h             pattern scanner, section walker, rel32 resolver
│
└── Injector/
    └── src/
        ├── main.cpp                 CLI + drag-drop + interactive picker + UAC
        ├── PEMapper.cpp/.h          manual PE mapper (alloc/reloc/IAT/TLS/DllMain)
        ├── Shellcode.h              position-independent remote stub (relocs + imports)
        └── ProcessUtil.cpp/.h       process enumeration, privilege, elevation
```

---

## Injector internals

Classical x64 manual mapping — no `LoadLibrary`, image never appears in `PEB_LDR_DATA`:

1. `OpenProcess(VM_* | CREATE_THREAD)` on target
2. `VirtualAllocEx(RWX, SizeOfImage)` — full image region
3. Carve PE sections locally → `WriteProcessMemory` full image
4. Build `ManualMapCtx`: image base, reloc/import/TLS dirs, entry RVA, base delta, `LoadLibraryA`/`GetProcAddress` pointers (same VA in all x64 processes per boot)
5. Write position-independent `ManualMapStub` into target
6. `CreateRemoteThread` → stub: apply DIR64 base relocs → resolve IAT → TLS callbacks → `DllMain(DLL_PROCESS_ATTACH)`
7. Zero PE headers in mapped region (removes from naive module scans)
8. Free ctx + stub allocations. Image stays resident.

---

## Limitations

- **No `RtlAddFunctionTable`** — C++ exceptions thrown across the injection boundary won't unwind correctly. The dumper avoids throwing; if you extend it, keep that in mind.
- **No per-section permission tightening** — all sections stay `RWX`. Fine for a dumper; tighten with `VirtualProtectEx` per section if stealth matters.
- **`AppendString` fallback** — if the sig misses, name resolution switches to a direct `FNamePool` block walk. Works on most games; custom pool stride/header encodings need `Offsets.h::FNamePool` tuning.

---

*SDKDumperUE - Made by xSerndev*
