#ifndef SHADER_UTILS_H
#define SHADER_UTILS_H

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdbool.h>

bool LoadShaderFile(const char *path, Uint8 **outBuffer, size_t *outSize);

#endif // SHADER_UTILS_H
