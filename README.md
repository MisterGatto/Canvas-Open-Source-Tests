# Canvas: Lua & GameGuardian Edition

> [!NOTE]
> **Project Scope & Notice**:
> This is a specialized **Lua & GameGuardian SIDE PROJECT** maintained alongside standard [Canvas](https://github.com/skyprotocol/Canvas-Open-Source).
> * **This Edition / Project**: Adds built-in Lua 5.4 scripting, native GameGuardian (`gg.*`) emulation, in-process memory scanning, and interactive ImGui dialogs.
> * **LuaLoader Mod**: The standalone `liblualoader.so` mod is maintained separately and distributed as precompiled binary releases for standard Canvas. (RELEASE TBD)
> * For Lua-specific questions or script support, please use this project's issues.
> * Updates here will be different even if I am updating Official Canvas.

---

## Features

* **Built-in Lua & GameGuardian Engine**: Run GameGuardian scripts directly in Sky without external virtual machines or GameGuardian apps.
* **In-Game ImGui Popups**: Interactive `gg.choice`, `gg.prompt`, `gg.alert`, and HUD `gg.toast` dialogs render natively over the game.
* **In-Process Memory Scanner**: Multi-region, chunked memory searching across DWORD, Float, Double, Byte, Word, QWord, XOR, and value ranges (`100~200`).
* **Pointer Scanning**: Live pointer offsets resolution with `gg.searchPointer`.
* **Built-in JSON & Live HTTP**: Pure-Lua JSON encoding/decoding and online script updates via `gg.makeRequest(url)`.
* **Obfuscation Compatibility**: Lua 5.1/5.2 polyfills (`setfenv`, `getfenv`, `loadstring`, `bit`, `bit32`).

---

## Documentation & Scripting

* [Lua Modding & GameGuardian API Reference](docs/LUA_MODDING_API.md) — Complete documentation for all `gg.*` and `canvas.*` functions.
* [Example Script](examples/)

---

## Credits & Licensing

* Upstream Canvas Mod Loader by [Artdev](https://github.com/artdeell)
* Embedded Lua 5.4.6 runtime (MIT License)
