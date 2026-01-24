# Build Instructions

## Windows (Visual Studio 2022/2026)
```powershell
cd SAR-Processor
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=23 --output-folder=build
cmake --preset conan-default
cmake --build build/build --config Debug
ctest --test-dir build/build -C Debug --output-on-failure
```

## Linux (GCC/Clang)
```bash
cd SAR-Processor
conan profile detect --force
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=23 --output-folder=build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
