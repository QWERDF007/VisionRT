#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(VISIONRT_TOOL_SDK_EXPORTS)
        #define VT_EXPORT __declspec(dllexport)
    #else
        #define VT_EXPORT
    #endif
    #define VT_CALL __cdecl
#else
    #define VT_EXPORT __attribute__((visibility("default")))
    #define VT_CALL
#endif
