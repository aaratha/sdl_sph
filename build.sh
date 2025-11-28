# Create build directory if it doesn't exist
mkdir -p build
cd build

# Compile Slang shaders to SPIR-V
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainCS -profile cs_6_0 -target spirv -o assets/particles.comp.spv
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainVS -profile vs_6_0 -target spirv -o assets/particles.vert.spv
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainPS -profile ps_6_0 -target spirv -o assets/particles.frag.spv

# Also emit Metal Shader Language sources for the native Metal backend.
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainCS -profile cs_6_0 -target metal -o assets/particles.comp.msl
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainVS -profile vs_6_0 -target metal -o assets/particles.vert.msl
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainPS -profile ps_6_0 -target metal -o assets/particles.frag.msl

# Run CMake to configure and build
cmake ..
cmake --build .

# Run with Metal (default on macOS)
./waveguide
