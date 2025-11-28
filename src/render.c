#include "render.h"

#include "shader_utils.h"

bool Render_Init(RenderState *state, SDL_GPUDevice *device,
                 SDL_GPUShaderFormat shaderFormat, const char *vertexShaderPath,
                 const char *fragmentShaderPath) {
  Uint8 *vertCode = NULL;
  size_t vertSize = 0;
  if (!LoadShaderFile(vertexShaderPath, &vertCode, &vertSize)) {
    return false;
  }

  SDL_GPUShaderCreateInfo vertShaderCreateInfo = {
      .code = vertCode,
      .code_size = vertSize,
      .entrypoint = "mainVS",
      .format = shaderFormat,
      .stage = SDL_GPU_SHADERSTAGE_VERTEX,
      .num_samplers = 0,
      .num_storage_textures = 0,
      // Bindings 1..6 are used for storage buffers, so expose 7 slots.
      .num_storage_buffers = 7,
      .num_uniform_buffers = 0};

  SDL_GPUShader *vertexShader =
      SDL_CreateGPUShader(device, &vertShaderCreateInfo);
  SDL_free(vertCode);
  if (vertexShader == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't create vertex shader: %s", SDL_GetError());
    return false;
  }

  Uint8 *fragCode = NULL;
  size_t fragSize = 0;
  if (!LoadShaderFile(fragmentShaderPath, &fragCode, &fragSize)) {
    SDL_ReleaseGPUShader(device, vertexShader);
    return false;
  }

  SDL_GPUShaderCreateInfo fragShaderCreateInfo = {
      .code = fragCode,
      .code_size = fragSize,
      .entrypoint = "mainPS",
      .format = shaderFormat,
      .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
      .num_samplers = 0,
      .num_storage_textures = 0,
      .num_storage_buffers = 0,
      .num_uniform_buffers = 0};

  SDL_GPUShader *fragmentShader =
      SDL_CreateGPUShader(device, &fragShaderCreateInfo);
  SDL_free(fragCode);
  if (fragmentShader == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't create fragment shader: %s", SDL_GetError());
    SDL_ReleaseGPUShader(device, vertexShader);
    return false;
  }

  SDL_GPUColorTargetDescription colorTargetDesc = {
      .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .blend_state = {.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                      .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                      .color_blend_op = SDL_GPU_BLENDOP_ADD,
                      .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                      .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                      .alpha_blend_op = SDL_GPU_BLENDOP_ADD}};

  SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo = {
      .vertex_shader = vertexShader,
      .fragment_shader = fragmentShader,
      .vertex_input_state =
          (SDL_GPUVertexInputState){.vertex_buffer_descriptions = NULL,
                                    .num_vertex_buffers = 0,
                                    .vertex_attributes = NULL,
                                    .num_vertex_attributes = 0},
      .primitive_type = SDL_GPU_PRIMITIVETYPE_POINTLIST,
      .rasterizer_state =
          (SDL_GPURasterizerState){.fill_mode = SDL_GPU_FILLMODE_FILL,
                                   .cull_mode = SDL_GPU_CULLMODE_NONE,
                                   .front_face =
                                       SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
                                   .depth_bias_constant_factor = 0.0f,
                                   .depth_bias_clamp = 0.0f,
                                   .depth_bias_slope_factor = 0.0f,
                                   .enable_depth_bias = false,
                                   .enable_depth_clip = true},
      .depth_stencil_state =
          (SDL_GPUDepthStencilState){
              .enable_depth_test = false,
              .enable_depth_write = false,
              .enable_stencil_test = false,
              .compare_op = SDL_GPU_COMPAREOP_ALWAYS,
              .front_stencil_state = (SDL_GPUStencilOpState){0},
              .back_stencil_state = (SDL_GPUStencilOpState){0},
              .compare_mask = 0,
              .write_mask = 0},
      .multisample_state = (SDL_GPUMultisampleState){.sample_count = 1},
      .target_info = (SDL_GPUGraphicsPipelineTargetInfo){
          .color_target_descriptions = &colorTargetDesc,
          .num_color_targets = 1,
          .depth_stencil_format = 0,
          .has_depth_stencil_target = false}};

  SDL_GPUGraphicsPipeline *pipeline =
      SDL_CreateGPUGraphicsPipeline(device, &pipelineCreateInfo);
  if (pipeline == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Couldn't create graphics pipeline: %s", SDL_GetError());
    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);
    return false;
  }

  state->vertexShader = vertexShader;
  state->fragmentShader = fragmentShader;
  state->pipeline = pipeline;
  return true;
}

void Render_Destroy(RenderState *state, SDL_GPUDevice *device) {
  if (state == NULL || device == NULL) {
    return;
  }
  if (state->pipeline != NULL) {
    SDL_ReleaseGPUGraphicsPipeline(device, state->pipeline);
  }
  if (state->vertexShader != NULL) {
    SDL_ReleaseGPUShader(device, state->vertexShader);
  }
  if (state->fragmentShader != NULL) {
    SDL_ReleaseGPUShader(device, state->fragmentShader);
  }
}

bool Render_Draw(RenderState *state, SDL_GPUCommandBuffer *cmdBuf,
                 SDL_Window *window, SDL_GPUBuffer *xCurr, SDL_GPUBuffer *yCurr,
                 int numParticles) {
  SDL_GPUTexture *swapchainTexture;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, window, &swapchainTexture,
                                             NULL, NULL)) {
    SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture: %s", SDL_GetError());
    return false;
  }

  if (swapchainTexture == NULL) {
    return true;
  }

  SDL_GPUColorTargetInfo targetInfo = {.texture = swapchainTexture,
                                       .cycle = true,
                                       .load_op = SDL_GPU_LOADOP_CLEAR,
                                       .store_op = SDL_GPU_STOREOP_STORE,
                                       .clear_color = {0.0f, 0.0f, 0.0f, 1.0f}};

  SDL_GPURenderPass *renderPass =
      SDL_BeginGPURenderPass(cmdBuf, &targetInfo, 1, NULL);
  if (renderPass == NULL) {
    SDL_Log("SDL_BeginGPURenderPass failed: %s", SDL_GetError());
    return false;
  }

  int viewportWidth = 0;
  int viewportHeight = 0;
  SDL_GetWindowSizeInPixels(window, &viewportWidth, &viewportHeight);
  SDL_GPUViewport viewport = {.x = 0.0f,
                              .y = 0.0f,
                              .w = (float)SDL_max(viewportWidth, 1),
                              .h = (float)SDL_max(viewportHeight, 1),
                              .min_depth = 0.0f,
                              .max_depth = 1.0f};
  SDL_SetGPUViewport(renderPass, &viewport);

  SDL_BindGPUGraphicsPipeline(renderPass, state->pipeline);

  SDL_GPUBuffer *buffers[] = {xCurr, yCurr};
  // Vertex shader expects x/y at storage slots 0 and 1.
  SDL_BindGPUVertexStorageBuffers(renderPass, 0, buffers, 2);

  SDL_DrawGPUPrimitives(renderPass, (Uint32)numParticles, 1, 0, 0);

  SDL_EndGPURenderPass(renderPass);
  return true;
}
