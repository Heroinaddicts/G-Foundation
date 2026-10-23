Run the focused manual ownership regression check from the Foundation directory:

```sh
c++ -std=c++20 -g -fsanitize=address,undefined -DFOUNDATION_MACOS \
  -I FoundationApi -I Utils Tests/ManualOwnership.cpp \
  Utils/JsonCpp/LunokiJsonReader.cpp Utils/JsonCpp/LunokiJsonValue.cpp \
  Utils/JsonCpp/LunokiJsonWriter.cpp -o Tests/ManualOwnership.check
./Tests/ManualOwnership.check
rm Tests/ManualOwnership.check
```

Covers expired network event handles, callback self-destruction and exceptions,
receive self-destruction, dispatch serialization for a live Session, JSON comment
copy/move ownership, JSON round trips, and reader/writer exception cleanup. Also
compiles the Excel headers. It does not validate SSH or a complete library build.
Session destruction occurs after producer threads have joined; concurrent derived
destruction is not supported by this check or guaranteed by the dispatch registry.

To also verify dispatch across a hidden-symbol macOS dynamic library, build the
fixture before running the executable (keep the library loaded until Sessions
and dispatches are gone):

```sh
c++ -std=c++20 -g -fsanitize=address,undefined -fvisibility=hidden -dynamiclib \
  -DFOUNDATION_MACOS -I FoundationApi -I Utils Tests/SessionModule.cpp \
  -o Tests/SessionModule.check.dylib
./Tests/ManualOwnership.check ./Tests/SessionModule.check.dylib
rm Tests/SessionModule.check.dylib
```
