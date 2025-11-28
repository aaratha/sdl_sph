// Before including SDL_main.h, define this to enable the new
// application lifecycle stuff.
#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "render.h"
#include "shader_utils.h"

// We'll have some things we want to keep track of as we move
// through the lifecycle functions. Globals would be fine for
// this example, but SDL gives you a way to pipe a data structure
// through the functions too, so we'll use that.
//
// For now, we need to keep track of the window we're creating
// and the GPU driver device.
typedef struct AppContext {
  SDL_Window *window;
  SDL_GPUDevice *device;
  RenderState render;
  SDL_GPUComputePipeline *computePipeline;
  SDL_GPUBuffer *xCurrBuffer;
  SDL_GPUBuffer *yCurrBuffer;
  SDL_GPUBuffer *xPrevBuffer;
  SDL_GPUBuffer *yPrevBuffer;
  SDL_GPUBuffer *massBuffer;
  SDL_GPUBuffer *densityBuffer;
  int numParticles;
} AppContext;

// SDL_AppInit is the first function that will be called. This is
// where you initialize SDL, load resources that your game will
// need from the start, etc.
SDL_AppResult SDL_AppInit(
    // Allows you to return a data structure to pass through
    void **appState,

    // Normal main argc & argv
    int argc, char **argv) {
  (void)argc;
  (void)argv;
  // This isn't strictly necessary, but if you provide a little
  // bit of metadata here SDL will use it in things like the
  // About window on macOS.
  SDL_SetAppMetadata("Waveguide", "0.0.1", "net.aaratha.Waveguide");

  // Seed the random number generator for particle initialization
  srand((unsigned int)time(NULL));

  // Initialize the video and event subsystems
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }

  // Create a window. I'm creating a high pixel density window
  // because without that, I was getting blurry text on macOS.
  // (text comes in a later post, promise.)
  SDL_WindowFlags windowFlags =
      SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;

  SDL_Window *window = SDL_CreateWindow("GPU by Example - Getting Started", 800,
                                        600, windowFlags);

  if (window == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create window: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }

  // Next up, let's create a GPU device. You'll need to tell the
  // API up front what shader languages you plan on supporting.
  // SDL looks through its list of drivers in "a reasonable
  // order" to pick which one to use. Fun surprise here: on
  // Windows, it's going to prefer Vulkan over Direct3D 12 if
  // it's available. Here, we're enabling Vulkan (SPIRV),
  // Direct3D 12 (DXIL), and Metal (MSL).
  SDL_GPUShaderFormat shaderFormats = SDL_GPU_SHADERFORMAT_SPIRV |
                                      SDL_GPU_SHADERFORMAT_DXIL |
                                      SDL_GPU_SHADERFORMAT_MSL;

  SDL_GPUDevice *device = SDL_CreateGPUDevice(shaderFormats, false, NULL);
  if (device == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't not create GPU device: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  // Just so we know what we're working with, log the driver that
  // SDL picked for us.
  const char *driverName = SDL_GetGPUDeviceDriver(device);
  SDL_Log("Using %s GPU implementation.", driverName ? driverName : "unknown");
  bool useMSLShaders =
      (driverName != NULL && SDL_strcmp(driverName, "metal") == 0);

  // Then bind the window and GPU device together
  if (!SDL_ClaimWindowForGPUDevice(device, window)) {
    SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  const char *vertexShaderPath =
      useMSLShaders ? "assets/particles.vert.msl" : "assets/particles.vert.spv";
  const char *fragmentShaderPath =
      useMSLShaders ? "assets/particles.frag.msl" : "assets/particles.frag.spv";
  const char *computeShaderPath =
      useMSLShaders ? "assets/particles.comp.msl" : "assets/particles.comp.spv";
  SDL_GPUShaderFormat shaderFormat =
      useMSLShaders ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV;

  // Load compute shader code and create compute pipeline
  Uint8 *compCode = NULL;
  size_t compSize = 0;
  if (!LoadShaderFile(computeShaderPath, &compCode, &compSize)) {
    return SDL_APP_FAILURE;
  }

  SDL_GPUComputePipelineCreateInfo computeCreateInfo = {
      .code_size = compSize,
      .code = compCode,
      .entrypoint = "mainCS",
      .format = shaderFormat,
      .num_samplers = 0,
      .num_readonly_storage_textures = 0,
      .num_readonly_storage_buffers = 0,
      .num_readwrite_storage_textures = 0,
  // Storage buffers bound at slots 0-5.
  .num_readwrite_storage_buffers = 6,
  // No uniforms.
  .num_uniform_buffers = 0,
      .threadcount_x = 64,
      .threadcount_y = 1,
      .threadcount_z = 1,
      .props = 0};

  SDL_GPUComputePipeline *computePipeline =
      SDL_CreateGPUComputePipeline(device, &computeCreateInfo);
  if (computePipeline == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't create compute pipeline: %s", SDL_GetError());
    SDL_free(compCode);
    return SDL_APP_FAILURE;
  }

  // Free the shader code as it's no longer needed after creating the shaders
  SDL_free(compCode);

  RenderState render = {0};
  if (!Render_Init(&render, device, shaderFormat, vertexShaderPath,
                   fragmentShaderPath)) {
    SDL_ReleaseGPUComputePipeline(device, computePipeline);
    return SDL_APP_FAILURE;
  }

  // Create buffer for particle data based on the Slang shader
  int drawableWidth = 0;
  int drawableHeight = 0;
  SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);
  if (drawableWidth == 0) {
    drawableWidth = 1;
  }
  if (drawableHeight == 0) {
    drawableHeight = 1;
  }
  const float halfWidth = drawableWidth * 0.5f;
  const float halfHeight = drawableHeight * 0.5f;

  const int numParticles =
      1024; // multiple of 64 to match compute threadgroup dispatch

  size_t floatBufferSize = sizeof(float) * (size_t)numParticles;
  float *xCurr = (float *)SDL_malloc(floatBufferSize);
  float *yCurr = (float *)SDL_malloc(floatBufferSize);
  float *xPrev = (float *)SDL_malloc(floatBufferSize);
  float *yPrev = (float *)SDL_malloc(floatBufferSize);
  float *mass = (float *)SDL_malloc(floatBufferSize);
  float *density = (float *)SDL_malloc(floatBufferSize);

  if (xCurr == NULL || yCurr == NULL || xPrev == NULL || yPrev == NULL ||
      mass == NULL || density == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't allocate particle data buffers");
    SDL_free(xCurr);
    SDL_free(yCurr);
    SDL_free(xPrev);
    SDL_free(yPrev);
    SDL_free(mass);
    SDL_free(density);
    Render_Destroy(&render, device);
    SDL_ReleaseGPUComputePipeline(device, computePipeline);
    return SDL_APP_FAILURE;
  }

  // Initialize particle data. Velocity is encoded via (curr - prev) so we pick
  // a small random drift.
  for (int i = 0; i < numParticles; i++) {
    float pixelX = (float)(rand() % drawableWidth);
    float pixelY = (float)(rand() % drawableHeight);
    float posX = (pixelX - halfWidth) / halfWidth;
    float posY = (pixelY - halfHeight) / halfHeight;

    float angle = ((float)rand() / (float)RAND_MAX) * 6.28318530718f; // 2*pi
    float speed = 0.004f + ((float)rand() / (float)RAND_MAX) *
                               0.006f; // 0.004..0.010 in NDC/frame
    float velX = cosf(angle) * speed;
    float velY = sinf(angle) * speed;

    xCurr[i] = posX + velX;
    yCurr[i] = posY + velY;
    xPrev[i] = posX;
    yPrev[i] = posY;
    mass[i] = 1.0f;
    density[i] = 0.0f;
  }

  SDL_GPUBufferCreateInfo bufferCreateInfo = {
      .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
               SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
               SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
      .size = floatBufferSize};

  SDL_GPUBuffer *xCurrBuffer = SDL_CreateGPUBuffer(device, &bufferCreateInfo);
  SDL_GPUBuffer *yCurrBuffer = SDL_CreateGPUBuffer(device, &bufferCreateInfo);
  SDL_GPUBuffer *xPrevBuffer = SDL_CreateGPUBuffer(device, &bufferCreateInfo);
  SDL_GPUBuffer *yPrevBuffer = SDL_CreateGPUBuffer(device, &bufferCreateInfo);
  SDL_GPUBuffer *massBuffer = SDL_CreateGPUBuffer(device, &bufferCreateInfo);
  SDL_GPUBuffer *densityBuffer = SDL_CreateGPUBuffer(device, &bufferCreateInfo);

  if (xCurrBuffer == NULL || yCurrBuffer == NULL || xPrevBuffer == NULL ||
      yPrevBuffer == NULL || massBuffer == NULL || densityBuffer == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't create particle buffers: %s", SDL_GetError());
    SDL_ReleaseGPUBuffer(device, xCurrBuffer);
    SDL_ReleaseGPUBuffer(device, yCurrBuffer);
    SDL_ReleaseGPUBuffer(device, xPrevBuffer);
    SDL_ReleaseGPUBuffer(device, yPrevBuffer);
    SDL_ReleaseGPUBuffer(device, massBuffer);
    SDL_ReleaseGPUBuffer(device, densityBuffer);
    SDL_free(xCurr);
    SDL_free(yCurr);
    SDL_free(xPrev);
    SDL_free(yPrev);
    SDL_free(mass);
    SDL_free(density);
    Render_Destroy(&render, device);
    SDL_ReleaseGPUComputePipeline(device, computePipeline);
    return SDL_APP_FAILURE;
  }

  // Create upload buffers for each attribute.
  SDL_GPUTransferBuffer *txXCurr = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                  .size = floatBufferSize});
  SDL_GPUTransferBuffer *txYCurr = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                  .size = floatBufferSize});
  SDL_GPUTransferBuffer *txXPrev = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                  .size = floatBufferSize});
  SDL_GPUTransferBuffer *txYPrev = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                  .size = floatBufferSize});
  SDL_GPUTransferBuffer *txMass = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                  .size = floatBufferSize});
  SDL_GPUTransferBuffer *txDensity = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                  .size = floatBufferSize});

  if (txXCurr == NULL || txYCurr == NULL || txXPrev == NULL ||
      txYPrev == NULL || txMass == NULL || txDensity == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't create transfer buffers: %s", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(device, txXCurr);
    SDL_ReleaseGPUTransferBuffer(device, txYCurr);
    SDL_ReleaseGPUTransferBuffer(device, txXPrev);
    SDL_ReleaseGPUTransferBuffer(device, txYPrev);
    SDL_ReleaseGPUTransferBuffer(device, txMass);
    SDL_ReleaseGPUTransferBuffer(device, txDensity);
    SDL_ReleaseGPUBuffer(device, xCurrBuffer);
    SDL_ReleaseGPUBuffer(device, yCurrBuffer);
    SDL_ReleaseGPUBuffer(device, xPrevBuffer);
    SDL_ReleaseGPUBuffer(device, yPrevBuffer);
    SDL_ReleaseGPUBuffer(device, massBuffer);
    SDL_ReleaseGPUBuffer(device, densityBuffer);
    SDL_free(xCurr);
    SDL_free(yCurr);
    SDL_free(xPrev);
    SDL_free(yPrev);
    SDL_free(mass);
    SDL_free(density);
    Render_Destroy(&render, device);
    SDL_ReleaseGPUComputePipeline(device, computePipeline);
    return SDL_APP_FAILURE;
  }

  struct {
    SDL_GPUTransferBuffer *tx;
    const float *data;
    SDL_GPUBuffer *gpuBuf;
  } uploads[] = {
      {txXCurr, xCurr, xCurrBuffer}, {txYCurr, yCurr, yCurrBuffer},
      {txXPrev, xPrev, xPrevBuffer}, {txYPrev, yPrev, yPrevBuffer},
      {txMass, mass, massBuffer},    {txDensity, density, densityBuffer},
  };

  bool mappingFailed = false;
  for (size_t i = 0; i < SDL_arraysize(uploads); i++) {
    void *mapped = SDL_MapGPUTransferBuffer(device, uploads[i].tx, false);
    if (mapped == NULL) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Couldn't map transfer buffer: %s", SDL_GetError());
      mappingFailed = true;
    } else {
      SDL_memcpy(mapped, uploads[i].data, floatBufferSize);
      SDL_UnmapGPUTransferBuffer(device, uploads[i].tx);
    }
  }

  if (mappingFailed) {
    SDL_ReleaseGPUTransferBuffer(device, txXCurr);
    SDL_ReleaseGPUTransferBuffer(device, txYCurr);
    SDL_ReleaseGPUTransferBuffer(device, txXPrev);
    SDL_ReleaseGPUTransferBuffer(device, txYPrev);
    SDL_ReleaseGPUTransferBuffer(device, txMass);
    SDL_ReleaseGPUTransferBuffer(device, txDensity);
    SDL_ReleaseGPUBuffer(device, xCurrBuffer);
    SDL_ReleaseGPUBuffer(device, yCurrBuffer);
    SDL_ReleaseGPUBuffer(device, xPrevBuffer);
    SDL_ReleaseGPUBuffer(device, yPrevBuffer);
    SDL_ReleaseGPUBuffer(device, massBuffer);
    SDL_ReleaseGPUBuffer(device, densityBuffer);
    SDL_free(xCurr);
    SDL_free(yCurr);
    SDL_free(xPrev);
    SDL_free(yPrev);
    SDL_free(mass);
    SDL_free(density);
    Render_Destroy(&render, device);
    SDL_ReleaseGPUComputePipeline(device, computePipeline);
    return SDL_APP_FAILURE;
  }

  SDL_GPUCommandBuffer *uploadCmdBuf = SDL_AcquireGPUCommandBuffer(device);
  if (uploadCmdBuf == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't acquire command buffer for upload: %s",
                 SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(device, txXCurr);
    SDL_ReleaseGPUTransferBuffer(device, txYCurr);
    SDL_ReleaseGPUTransferBuffer(device, txXPrev);
    SDL_ReleaseGPUTransferBuffer(device, txYPrev);
    SDL_ReleaseGPUTransferBuffer(device, txMass);
    SDL_ReleaseGPUTransferBuffer(device, txDensity);
    SDL_ReleaseGPUBuffer(device, xCurrBuffer);
    SDL_ReleaseGPUBuffer(device, yCurrBuffer);
    SDL_ReleaseGPUBuffer(device, xPrevBuffer);
    SDL_ReleaseGPUBuffer(device, yPrevBuffer);
    SDL_ReleaseGPUBuffer(device, massBuffer);
    SDL_ReleaseGPUBuffer(device, densityBuffer);
    SDL_free(xCurr);
    SDL_free(yCurr);
    SDL_free(xPrev);
    SDL_free(yPrev);
    SDL_free(mass);
    SDL_free(density);
    Render_Destroy(&render, device);
    SDL_ReleaseGPUComputePipeline(device, computePipeline);
    return SDL_APP_FAILURE;
  }

  SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);
  if (copyPass == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't begin copy pass: %s",
                 SDL_GetError());
    SDL_SubmitGPUCommandBuffer(uploadCmdBuf);
    SDL_ReleaseGPUTransferBuffer(device, txXCurr);
    SDL_ReleaseGPUTransferBuffer(device, txYCurr);
    SDL_ReleaseGPUTransferBuffer(device, txXPrev);
    SDL_ReleaseGPUTransferBuffer(device, txYPrev);
    SDL_ReleaseGPUTransferBuffer(device, txMass);
    SDL_ReleaseGPUTransferBuffer(device, txDensity);
    SDL_ReleaseGPUBuffer(device, xCurrBuffer);
    SDL_ReleaseGPUBuffer(device, yCurrBuffer);
    SDL_ReleaseGPUBuffer(device, xPrevBuffer);
    SDL_ReleaseGPUBuffer(device, yPrevBuffer);
    SDL_ReleaseGPUBuffer(device, massBuffer);
    SDL_ReleaseGPUBuffer(device, densityBuffer);
    SDL_free(xCurr);
    SDL_free(yCurr);
    SDL_free(xPrev);
    SDL_free(yPrev);
    SDL_free(mass);
    SDL_free(density);
    Render_Destroy(&render, device);
    SDL_ReleaseGPUComputePipeline(device, computePipeline);
    return SDL_APP_FAILURE;
  }

  for (size_t i = 0; i < SDL_arraysize(uploads); i++) {
    SDL_GPUTransferBufferLocation src = {.transfer_buffer = uploads[i].tx,
                                         .offset = 0};
    SDL_GPUBufferRegion dst = {
        .buffer = uploads[i].gpuBuf, .offset = 0, .size = floatBufferSize};
    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
  }

  SDL_EndGPUCopyPass(copyPass);
  SDL_SubmitGPUCommandBuffer(uploadCmdBuf);

  SDL_ReleaseGPUTransferBuffer(device, txXCurr);
  SDL_ReleaseGPUTransferBuffer(device, txYCurr);
  SDL_ReleaseGPUTransferBuffer(device, txXPrev);
  SDL_ReleaseGPUTransferBuffer(device, txYPrev);
  SDL_ReleaseGPUTransferBuffer(device, txMass);
  SDL_ReleaseGPUTransferBuffer(device, txDensity);

  SDL_free(xCurr);
  SDL_free(yCurr);
  SDL_free(xPrev);
  SDL_free(yPrev);
  SDL_free(mass);
  SDL_free(density);

  // Last up, let's create our context object and store pointers
  // to our window and GPU device. We stick it in the appState
  // argument passed to this function and SDL will provide it in
  // later calls.
  AppContext *context = SDL_calloc(1, sizeof(AppContext));
  if (context == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't allocate app context");
    SDL_ReleaseGPUBuffer(device, xCurrBuffer);
    SDL_ReleaseGPUBuffer(device, yCurrBuffer);
    SDL_ReleaseGPUBuffer(device, xPrevBuffer);
    SDL_ReleaseGPUBuffer(device, yPrevBuffer);
    SDL_ReleaseGPUBuffer(device, massBuffer);
    SDL_ReleaseGPUBuffer(device, densityBuffer);
    SDL_ReleaseGPUComputePipeline(device, computePipeline);
    Render_Destroy(&render, device);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return SDL_APP_FAILURE;
  }

  context->window = window;
  context->device = device;
  context->computePipeline = computePipeline;
  context->render = render;
  context->xCurrBuffer = xCurrBuffer;
  context->yCurrBuffer = yCurrBuffer;
  context->xPrevBuffer = xPrevBuffer;
  context->yPrevBuffer = yPrevBuffer;
  context->massBuffer = massBuffer;
  context->densityBuffer = densityBuffer;
  context->numParticles = numParticles;
  *appState = context;

  // And that's it for initialization.
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appState) {
  // Our AppContext instance is passed in through the appState
  // pointer.
  AppContext *context = (AppContext *)appState;

  // Generally speaking, this is where you'd track frame times,
  // update your game state, etc. I'll be doing that in later
  // posts.

  // Once you're ready to start drawing, begin by grabbing a
  // command buffer and a reference to the swapchain texture.
  SDL_GPUCommandBuffer *cmdBuf;
  cmdBuf = SDL_AcquireGPUCommandBuffer(context->device);
  if (cmdBuf == NULL) {
    SDL_Log("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  // GPU compute integration of particle positions.
  Uint32 groupCountX = (Uint32)((context->numParticles + 63) / 64);
  SDL_GPUStorageBufferReadWriteBinding rwBindings[] = {
      {.buffer = context->xCurrBuffer, .cycle = false},
      {.buffer = context->yCurrBuffer, .cycle = false},
      {.buffer = context->xPrevBuffer, .cycle = false},
      {.buffer = context->yPrevBuffer, .cycle = false},
      {.buffer = context->massBuffer, .cycle = false},
      {.buffer = context->densityBuffer, .cycle = false},
  };
  SDL_GPUComputePass *computePass = SDL_BeginGPUComputePass(
      cmdBuf, NULL, 0, rwBindings, SDL_arraysize(rwBindings));
  if (computePass == NULL) {
    SDL_Log("SDL_BeginGPUComputePass failed: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  SDL_BindGPUComputePipeline(computePass, context->computePipeline);

  SDL_GPUBuffer *computeBuffers[] = {
      context->xCurrBuffer, context->yCurrBuffer, context->xPrevBuffer,
      context->yPrevBuffer, context->massBuffer,  context->densityBuffer};
  // Storage buffers bound at slots 0-5 to match shader bindings.
  SDL_BindGPUComputeStorageBuffers(computePass, 0, computeBuffers,
                                   SDL_arraysize(computeBuffers));
  SDL_DispatchGPUCompute(computePass, groupCountX, 1, 1);
  SDL_EndGPUComputePass(computePass);

  if (!Render_Draw(&context->render, cmdBuf, context->window,
                   context->xCurrBuffer, context->yCurrBuffer,
                   context->numParticles)) {
    return SDL_APP_FAILURE;
  }

  // And finally, submit the command buffer for drawing. The
  // driver will take over at this point and do all the rendering
  // we've asked it to.
  SDL_SubmitGPUCommandBuffer(cmdBuf);

  // That's it for this frame.
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appState, SDL_Event *event) {
  (void)appState;

  // SDL_EVENT_QUIT is sent when the main (last?) application
  // window closes.
  if (event->type == SDL_EVENT_QUIT) {
    // SDL_APP_SUCCESS means we're making a clean exit.
    // SDL_APP_FAILURE would mean something went wrong.
    return SDL_APP_SUCCESS;
  }

  // For convenience, I'm also quitting when the user presses the
  // escape key. It makes life easier when I'm testing on a Steam
  // Deck.
  if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
    return SDL_APP_SUCCESS;
  }

  // Nothing else to do, so just continue on with the next frame
  // or event.
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appState, SDL_AppResult result) {
  (void)result;

  AppContext *context = (AppContext *)appState;

  // Just cleaning things up, making sure we're working with
  // valid pointers as we go.
  if (context != NULL) {
    if (context->device != NULL) {
      if (context->computePipeline != NULL) {
        SDL_ReleaseGPUComputePipeline(context->device,
                                      context->computePipeline);
      }
      Render_Destroy(&context->render, context->device);
      if (context->xCurrBuffer != NULL) {
        SDL_ReleaseGPUBuffer(context->device, context->xCurrBuffer);
      }
      if (context->yCurrBuffer != NULL) {
        SDL_ReleaseGPUBuffer(context->device, context->yCurrBuffer);
      }
      if (context->xPrevBuffer != NULL) {
        SDL_ReleaseGPUBuffer(context->device, context->xPrevBuffer);
      }
      if (context->yPrevBuffer != NULL) {
        SDL_ReleaseGPUBuffer(context->device, context->yPrevBuffer);
      }
      if (context->massBuffer != NULL) {
        SDL_ReleaseGPUBuffer(context->device, context->massBuffer);
      }
      if (context->densityBuffer != NULL) {
        SDL_ReleaseGPUBuffer(context->device, context->densityBuffer);
      }

      if (context->window != NULL) {
        SDL_ReleaseWindowFromGPUDevice(context->device, context->window);
        SDL_DestroyWindow(context->window);
      }

      SDL_DestroyGPUDevice(context->device);
    }

    SDL_free(context);
  }

  SDL_Quit();
}
