#include "shader_utils.h"

#include <stdio.h>

bool LoadShaderFile(const char *path, Uint8 **outBuffer, size_t *outSize) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open shader file: %s", path);
    return false;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't seek shader file: %s", path);
    fclose(file);
    return false;
  }

  long length = ftell(file);
  if (length < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't determine shader file size: %s", path);
    fclose(file);
    return false;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't rewind shader file: %s", path);
    fclose(file);
    return false;
  }

  Uint8 *buffer = (Uint8 *)SDL_malloc((size_t)length + 1);
  if (buffer == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't allocate memory for shader: %s", path);
    fclose(file);
    return false;
  }

  size_t read = fread(buffer, 1, (size_t)length, file);
  fclose(file);
  if (read != (size_t)length) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't read entire shader file: %s", path);
    SDL_free(buffer);
    return false;
  }

  buffer[length] = '\0';  // Keep text shaders null-terminated for safety.
  *outBuffer = buffer;
  *outSize = (size_t)length;
  return true;
}
