#ifndef RENDER_H
#define RENDER_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct RenderState {
  SDL_GPUShader *vertexShader;
  SDL_GPUShader *fragmentShader;
  SDL_GPUGraphicsPipeline *pipeline;
} RenderState;

bool Render_Init(RenderState *state,
                 SDL_GPUDevice *device,
                 SDL_GPUShaderFormat shaderFormat,
                 const char *vertexShaderPath,
                 const char *fragmentShaderPath);

void Render_Destroy(RenderState *state, SDL_GPUDevice *device);

bool Render_Draw(RenderState *state,
                 SDL_GPUCommandBuffer *cmdBuf,
                 SDL_Window *window,
                 SDL_GPUBuffer *xCurr,
                 SDL_GPUBuffer *yCurr,
                 int numParticles);

#endif // RENDER_H
