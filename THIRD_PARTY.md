# Third-party components

- **Dear ImGui v1.91.9b**, by Omar Cornut and contributors, MIT license. Fetched from https://github.com/ocornut/imgui at configure time. Includes its Win32 backend and an application-owned adaptation of its DirectX 12 backend in `src/UiRenderer.cpp`. The adaptation consumes copied draw commands and isolates backend state from the main-thread ImGui context. The complete license is in `build/_deps/imgui-src/LICENSE.txt` and is copied alongside the executable when building.
- **Windows SDK / DirectXMath / DXC** are supplied by the installed Microsoft development tools and used under their respective Microsoft SDK licenses. DXC runs during the build; no compiler DLL is required alongside the application.
- **Segoe UI** is loaded from the user's Windows installation. The font is not redistributed; the interface falls back to Dear ImGui's built-in font if absent.

No game engine, external assets, or runtime network services are used.

The packaged build includes app-local Microsoft Visual C++ 143 runtime DLLs from the installed Visual Studio redistributable directory, under the applicable Microsoft redistribution terms.
