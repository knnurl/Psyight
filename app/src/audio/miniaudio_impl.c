// Compiles the miniaudio implementation once. miniaudio is a single-header library
// (public domain or MIT-0); every other file includes <miniaudio.h> without the define.

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
