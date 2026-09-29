# StringLoader — Resource String Loader

`StringLoader.lib` is a small static library for Windows CE and Windows Phone 7.
It comes from the FullUnlock v4.0 project (© Maxim Menshikov (ultrashot), 2012). It loads localized
string resources from a module and resolves the caller's locale. The companion
helper `uplhlp` links this library to localize the text it shows.

This is legacy research and homebrew code for a platform that reached end of
life long ago.

## How it works

The library exports two C functions (see `StringLoader.def`):

- `ReadResourceString(Dll, ResourceID, Result)` loads one string resource. It
  maps the module file into memory (`ReadFileComplete`). It walks the raw PE
  resource directory to the string table for the requested id. It chooses the
  best language match in this order: system locale, primary language, neutral,
  English. If that hand-rolled path fails, it falls back to the OS
  `LoadLibraryEx` and `LoadString`. It resolves a bare module name under
  `\Windows`.
- `ReadFileComplete(FileName, Result)` reads a whole file into a freshly
  allocated buffer.

`wp7res_mgr.cpp` holds the implementation. `StringLoader.cpp` is only the DLL
entry point.

## Layout

```
StringLoader/
├── StringLoader.vcproj   Visual Studio 2008 project (WM6 Pro ARMv4I, static lib)
├── .clang-format         Formatting rules for src/
├── src/                  Project sources
│   ├── wp7res_mgr.cpp/.h    Resource-string loader implementation
│   ├── StringLoader.cpp     DllMain
│   ├── StringLoader.def     Exported entry points
│   └── stdafx.h/.cpp        Precompiled-header stub
└── sdk/                  Vendored import library
    └── coredll7.lib
```

## Building

You need Visual Studio 2008 with the Windows Mobile 6 Professional SDK (ARMV4I)
installed. To build the library:

1. Open `StringLoader.vcproj`.
2. Build the `Release_Lib` configuration.

The output is the static library `StringLoader.lib`. The include and library
paths point at `src/` and `sdk/`. The project is self-contained and does not need
an enclosing solution.
