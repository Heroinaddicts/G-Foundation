// 一个可以加载、但没有 Runtime 工厂符号的动态库。
#if defined(_WIN32)
extern "C" __declspec(dllexport) int FixtureMarker() { return 1; }
#else
extern "C" __attribute__((visibility("default"))) int FixtureMarker() { return 1; }
#endif
