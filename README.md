# Vulkan physical based rendering using glTF 2.0 models

<img src="./screenshots/finalresult.png" width="640px">

## About

Physical based rendering example implementation with image based lighting in Vulkan using glTF 2.0 models with a meallic/roughness workflow.

## Texture map generation

The phyiscal based render model uses multiple source images for the lighting equation. Instead of relying on offline tools to generate those, this example will generate all required texture maps during startup using the GPU.

### BRDF lookup table

<img src="./screenshots/tex_brdflut.png" width="256px">

This pass generates a 2D BRDF lookup table based on the formulas used in the pbr implementation. The lookup table contains BRDF values for roughness and view angle and is stored as a 16-bit per component floating point texture to provide proper precision.

See ```VulkanExample::generateBRDFLUT()```

### Irradiance cube map

<img src="./screenshots/tex_envmap.png" width="256px"> <img src="./screenshots/tex_irradiance_cube.png" width="256px">

(*left*: front face environment cube map / *right*: front face irradiance cube map)

Generates a small (64 x 64 px) irradiance cube map from that stores the light radiated from the surrounding environment. This is sampled for the **indirect diffuse** part of the lighting equation.

See ```VulkanExample::generateCubemaps()``` with target ```IRRADIANCE```

### Pre-filtered (mip-mapped radiance) environment cube map

<img src="./screenshots/tex_envmap.png" width="256px"> <img src="./screenshots/tex_prefiltered_cube.png" width="256px">

(*left*: front face environment cube map / *right*: front face prefiltered cube map)

Generates a pre-filtered radiance cube map that is sampled for the **indirect specular** part of the lighting equation and stores specular contribution based on roughness. The mip chain stores increasing roughness values with increasing level count and is sampled accordingly when rendering the object.

Complete mip chain from *left* roughness = 0.0 to *right* roughenss = 1.0:

<img src="./screenshots/tex_prefiltered_cube_mipchain_0.png"> <img src="./screenshots/tex_prefiltered_cube_mipchain_1.png"> <img src="./screenshots/tex_prefiltered_cube_mipchain_2.png"> <img src="./screenshots/tex_prefiltered_cube_mipchain_3.png"> <img src="./screenshots/tex_prefiltered_cube_mipchain_4.png"> <img src="./screenshots/tex_prefiltered_cube_mipchain_5.png"> <img src="./screenshots/tex_prefiltered_cube_mipchain_6.png"> <img src="./screenshots/tex_prefiltered_cube_mipchain_7.png"> <img src="./screenshots/tex_prefiltered_cube_mipchain_8.png"> <img src="./screenshots/tex_prefiltered_cube_mipchain_9.png">

(*note:* down/up sized to same size for visualization)

See ```VulkanExample::generateCubemaps()``` with target ```PREFILTEREDENV```

## Links
* [glTF format specification](https://github.com/KhronosGroup/glTF)
* [tiny glTF library](https://github.com/syoyo/tinygltf)
