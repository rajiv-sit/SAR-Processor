# Codex Restart Reminder

## Context
- Repo: `C:\Users\MrSit\source\repos\SAR-processing\SAR-Processor`
- MATLAB source: `C:\Users\MrSit\source\repos\SAR-processing`
- Visual Studio install just completed; machine restart pending.

## Current Status
- Working tree was clean (`git status -sb` showed no changes).
- Build/test has not been run after the new VS install.
- Conan toolchain previously failed due to toolset/SDK mismatch; VS upgrade should fix it.
- `md/MATLAB-to-Cplusplus.md` is now in `.gitignore` (remove from index if already tracked).

## Next Commands (after restart)
Run from `SAR-Processor`:
```powershell
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=23 --output-folder=build
cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

If Conan/SDK still errors, share the exact error text to adjust the profile.

## Recent Work (high level)
- Added RPF line streaming + file discovery/query helpers and tests.
- Added PTA multi-peak detection, histogram generation, `.prs` parsing stub and tests.
- Added back-projection filter window + TIFF stub output and tests.
- Updated HLD coverage checklist and MATLAB-to-C++ mapping.
