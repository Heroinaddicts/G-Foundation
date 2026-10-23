# Project rules

- Do not use smart pointers or automatic owning pointer wrappers in first-party
  project code: `std::unique_ptr`, `std::shared_ptr`,
  `std::weak_ptr`, `std::auto_ptr`, `std::make_unique`, `std::make_shared`, or
  equivalent ownership wrappers.
- Leave bundled third-party libraries, such as JsonCpp and Excel, unchanged.
  Their internal smart pointer use is outside this rule's scope.
- GRHI consumers use its public API and prebuilt runtime through `GRHI.cmake`.
  Do not add GRHI implementation sources or its source-build subdirectory to
  GameSprout launcher, module, or view projects. Build GRHI independently.
- Foundation consumers use public APIs and prebuilt libraries. Do not add
  Foundation implementation sources or its source-build subdirectory to
  GameSprout projects. Build Foundation independently via its CMakeLists.txt;
  use FoundationSDK.cmake for runtime deployment metadata.
- Use ordinary pointers and explicit allocation, ownership transfer, and cleanup.
  Preserve cleanup on failure and exception paths. Owning classes must disable
  copying or implement a deliberate deep copy.
- GameSprout initialization and the first singleton access run on the main
  thread. Do not add synchronization or reference counting to these paths.
  Other thread access and lifetime coordination are handled separately by the
  developer; pointer ownership does not establish thread safety.
- Define GameSprout's authored widget trees, static layouts, visual properties,
  default menu actions, and component styles in `.ui` forms. Reuse form-backed
  components rather than building widgets/layouts or styling instances in C++.
  C++ may instantiate forms, connect events, bind runtime data/state, and adapt
  native docking geometry. Dynamically registered menu entries remain data-driven.
  Do not edit generated `ui_*.h` files; register forms with CMake AUTOUIC.
