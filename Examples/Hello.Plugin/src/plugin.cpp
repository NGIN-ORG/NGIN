#if defined(_WIN32)
#define HELLO_PLUGIN_EXPORT __declspec(dllexport)
#else
#define HELLO_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C" HELLO_PLUGIN_EXPORT auto HelloPluginVersion() -> int
{
    return 1;
}
