# Create build directory if it doesn't exist
mkdir -p build
cd build

# Where shaders should be written. Point at the source tree so CMake's asset
# copy picks up the freshly compiled versions.
OUTDIR=${PROJECT_SOURCE_DIR:-..}/assets

# Compile Slang shaders to SPIR-V
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainCS -profile cs_6_0 -target spirv -o ${OUTDIR}/particles.comp.spv
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainVS -profile vs_6_0 -target spirv -o ${OUTDIR}/particles.vert.spv
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainPS -profile ps_6_0 -target spirv -o ${OUTDIR}/particles.frag.spv

# Also emit Metal Shader Language sources for the native Metal backend.
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainCS -profile cs_6_0 -target metal -o ${OUTDIR}/particles.comp.msl
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainVS -profile vs_6_0 -target metal -o ${OUTDIR}/particles.vert.msl
slangc ${PROJECT_SOURCE_DIR:-..}/assets/particles.slang -entry mainPS -profile ps_6_0 -target metal -o ${OUTDIR}/particles.frag.msl

# Run CMake to configure and build
cmake ..
cmake --build .

# Run with Metal (default on macOS)
./waveguide
