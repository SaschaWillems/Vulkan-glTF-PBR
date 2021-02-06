// Shaders taken from the Khronos glTF sample viewer and adopted for Vulkan https://github.com/KhronosGroup/glTF-Sample-Viewer

//
// This fragment shader defines a reference implementation for Physically Based Shading of
// a microfacet surface material defined by a glTF model.
//
// References:
// [1] Real Shading in Unreal Engine 4
//     http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// [2] Physically Based Shading at Disney
//     http://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
// [3] README.md - Environment Maps
//     https://github.com/KhronosGroup/glTF-WebGL-PBR/#environment-maps
// [4] "An Inexpensive BRDF Model for Physically based Rendering" by Christophe Schlick
//     https://www.cs.virginia.edu/~jdl/bib/appearance/analytic%20models/schlick94b.pdf
// [5] "KHR_materials_clearcoat"
//     https://github.com/ux3d/glTF/tree/KHR_materials_pbrClearcoat/extensions/2.0/Khronos/KHR_materials_clearcoat
// [6] "KHR_materials_specular"
//     https://github.com/ux3d/glTF/tree/KHR_materials_pbrClearcoat/extensions/2.0/Khronos/KHR_materials_specular
// [7] "KHR_materials_subsurface"
//     https://github.com/KhronosGroup/glTF/pull/1766
// [8] "KHR_materials_thinfilm"
//     https://github.com/ux3d/glTF/tree/extensions/KHR_materials_thinfilm/extensions/2.0/Khronos/KHR_materials_thinfilm

#version 450

#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV0;
layout (location = 3) in vec2 inUV1;

// Scene bindings

layout (set = 0, binding = 0) uniform UBO {
	mat4 projection;
	mat4 model;
	mat4 view;
	vec3 camPos;
} ubo;

layout (set = 0, binding = 1) uniform UBOParams {
	vec4 lightDir;
	float exposure;
	float gamma;
	float prefilteredCubeMipLevels;
	float scaleIBLAmbient;
	float debugViewInputs;
	float debugViewEquation;
    int toneMapper;
} uboParams;

layout (set = 0, binding = 2) uniform samplerCube envLambertian;
layout (set = 0, binding = 3) uniform samplerCube envGGX;
layout (set = 0, binding = 4) uniform sampler2D lutBRDF;
layout (set = 0, binding = 5) uniform sampler2D lutCharlie;
layout (set = 0, binding = 6) uniform sampler2D lutGGX;
layout (set = 0, binding = 7) uniform sampler2D lutThinFilm;

// Material bindings

struct Material {
	vec4 baseColorFactor;
	vec4 emissiveFactor;
	vec4 diffuseFactor;
	vec4 specularFactor;

	float workflow;
	int baseColorTextureSet;
	int physicalDescriptorTextureSet;
	int normalTextureSet;	

	int occlusionTextureSet;
	int emissiveTextureSet;
	float metallicFactor;	
	float roughnessFactor;	

	float alphaMask;	
	float alphaMaskCutoff;
	float _pad0;
	float _pad1;

	vec4 sheenColorFactorAndRoughness;
    
	int materialType;
    int hasMetallicRoughness;
	int hasSpecularGlossiness;
	int hasClearcoat;

    vec2 clearcoatFactorAndRoughness;
    int clearcoatTextureSet;
    int clearcoatRoughnessTextureSet;
    int clearcoatNormalTextureSet;
};

layout (set = 1, binding = 0) uniform sampler2D colorMap;
layout (set = 1, binding = 1) uniform sampler2D physicalDescriptorMap;
layout (set = 1, binding = 2) uniform sampler2D normalMap;
layout (set = 1, binding = 3) uniform sampler2D aoMap;
layout (set = 1, binding = 4) uniform sampler2D emissiveMap;
layout (set = 1, binding = 5) uniform sampler2D clearcoatMap;
layout (set = 1, binding = 6) uniform sampler2D clearcoatRoughnessMap;
layout (set = 1, binding = 7) uniform sampler2D clearcoatNormalMap;
layout (set = 1, binding = 8) readonly buffer Materials { Material materials[]; };

layout (push_constant) uniform PushConstant {
	int materialIndex;
} pushConstant;

layout (location = 0) out vec4 finalColor;

// @todo: replace with flags from material description
const float PBR_WORKFLOW_METALLIC_ROUGHNESS = 0.0;
const float PBR_WORKFLOW_SPECULAR_GLOSINESS = 1.0;

#include "includes/tonemapping.glsl"
#include "includes/functions.glsl"
#include "includes/brdf.glsl"
//#include "includes/punctual.glsl"
#include "includes/ibl.glsl"

#ifdef USE_PUNCTUAL
// @todo
// uniform Light u_Lights[LIGHT_COUNT];
#endif

// Specular
// @todo
// uniform float u_MetallicRoughnessSpecularFactor;

// Anisotropy
// @todo
//uniform float u_Anisotropy;
//uniform vec3 u_AnisotropyDirection;

// Subsurface
// @todo
//uniform float u_SubsurfaceScale;
//uniform float u_SubsurfaceDistortion;
//uniform float u_SubsurfacePower;
//uniform vec3 u_SubsurfaceColorFactor;
//uniform float u_SubsurfaceThicknessFactor;

// Thin Film
// @todo
//uniform float u_ThinFilmFactor;
//uniform float u_ThinFilmThicknessMinimum;
//uniform float u_ThinFilmThicknessMaximum;

// IOR (in .x) and the corresponding f0 (in .y)
// @todo
// uniform vec2 u_IOR_and_f0;

// Thickness
// @todo
//uniform float u_Thickness;

// Absorption
// @todo
//uniform vec3 u_AbsorptionColor;

// Transmission
// @todo
//uniform float u_Transmission;

struct MaterialInfo
{
    float perceptualRoughness;      // roughness value, as authored by the model creator (input to shader)
    vec3 f0;                        // full reflectance color (n incidence angle)

    float alphaRoughness;           // roughness mapped to a more linear change in the roughness (proposed by [2])
    vec3 albedoColor;

    vec3 f90;                       // reflectance color at grazing angle
    float metallic;

    vec3 n;
    vec3 baseColor; // getBaseColor()

    float sheenIntensity;
    vec3 sheenColor;
    float sheenRoughness;

    float anisotropy;

    vec3 clearcoatF0;
    vec3 clearcoatF90;
    float clearcoatFactor;
    vec3 clearcoatNormal;
    float clearcoatRoughness;

    float subsurfaceScale;
    float subsurfaceDistortion;
    float subsurfacePower;
    vec3 subsurfaceColor;
    float subsurfaceThickness;

    float thinFilmFactor;
    float thinFilmThickness;

    float thickness;

    vec3 absorption;

    float transmission;
};

vec4 getVertexColor()
{
   // @todo: not yet suppored
   vec4 color = vec4(1.0, 1.0, 1.0, 1.0);
   return color;
}

// Get normal, tangent and bitangent vectors.
NormalInfo getNormalInfo(Material material, vec3 v)
{
    vec2 UV = material.normalTextureSet == 0 ? inUV0 : inUV1;
    vec3 uv_dx = dFdx(vec3(UV, 0.0));
    vec3 uv_dy = dFdy(vec3(UV, 0.0));

    vec3 t_ = (uv_dy.t * dFdx(inWorldPos) - uv_dx.t * dFdy(inWorldPos)) /
        (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);

    vec3 n, t, b, ng;

    // Compute geometrical TBN:
//    #ifdef HAS_TANGENTS
//        // Trivial TBN computation, present as vertex attribute.
//        // Normalize eigenvectors as matrix is linearly interpolated.
//        t = normalize(v_TBN[0]);
//        b = normalize(v_TBN[1]);
//        ng = normalize(v_TBN[2]);
//    #else
        ng = normalize(inNormal);

        t = normalize(t_ - ng * dot(ng, t_));
        b = cross(ng, t);

//    #endif

    // For a back-facing surface, the tangential basis vectors are negated.
    float facing = step(0.0, dot(v, ng)) * 2.0 - 1.0;
    t *= facing;
    b *= facing;
    ng *= facing;

    // Due to anisoptry, the tangent can be further rotated around the geometric normal.
    vec3 direction;
    #ifdef MATERIAL_ANISOTROPY
        #ifdef HAS_ANISOTROPY_DIRECTION_MAP
            direction = texture(u_AnisotropyDirectionSampler, getAnisotropyDirectionUV()).xyz * 2.0 - vec3(1.0);
        #else
            direction = u_AnisotropyDirection;
        #endif
    #else
        direction = vec3(1.0, 0.0, 0.0);
    #endif
    mat3 tbn = mat3(t, b, ng);
    t = tbn * normalize(direction);
    b = normalize(cross(ng, t));

    // Compute pertubed normals:
    if (material.normalTextureSet > -1) {
        n = texture(normalMap, UV).rgb * 2.0 - 1.0;
//        n *= vec3(u_NormalScale, u_NormalScale, 1.0);
        n = normalize(tbn * n);
    } else {
        n = ng;
    }

    NormalInfo info;
    info.ng = ng;
    info.t = t;
    info.b = b;
    info.n = n;
    return info;
}

vec4 getBaseColor(Material material)
{
    vec4 baseColor = vec4(1, 1, 1, 1);

    if (material.workflow == PBR_WORKFLOW_METALLIC_ROUGHNESS) {
        baseColor = material.baseColorFactor;
        baseColor *= sRGBToLinear(texture(colorMap, material.baseColorTextureSet == 0 ? inUV0 : inUV1));
    } else {
        if (material.workflow == PBR_WORKFLOW_SPECULAR_GLOSINESS) {
        // @todo        
//        baseColor = u_DiffuseFactor;
//        baseColor *= sRGBToLinear(texture(u_DiffuseSampler, getDiffuseUV()));
        }
    }

    return baseColor * getVertexColor();
}

MaterialInfo getSpecularGlossinessInfo(Material material, MaterialInfo info)
{
    info.f0 = material.specularFactor.rgb;
    // @todo
//    info.perceptualRoughness = u_GlossinessFactor;
    info.perceptualRoughness = 1.0;

	if (material.physicalDescriptorTextureSet > -1) {
        vec4 sgSample = texture(physicalDescriptorMap, material.physicalDescriptorTextureSet == 0 ? inUV0 : inUV1);
        info.perceptualRoughness *= sgSample.a ; // glossiness to roughness
        info.f0 *= sgSample.rgb; // specular
    }

    info.perceptualRoughness = 1.0 - info.perceptualRoughness; // 1 - glossiness
    info.albedoColor = info.baseColor.rgb * (1.0 - max(max(info.f0.r, info.f0.g), info.f0.b));

    return info;
}

// KHR_extension_specular alters f0 on metallic materials based on the specular factor specified in the extention
float getMetallicRoughnessSpecularFactor()
{
// @todo
/*
    //F0 = 0.08 * specularFactor * specularTexture
#ifdef HAS_METALLICROUGHNESS_SPECULAROVERRIDE_MAP
    vec4 specSampler =  texture(u_MetallicRoughnessSpecularSampler, getMetallicRoughnessSpecularUV());
    return 0.08 * u_MetallicRoughnessSpecularFactor * specSampler.a;
#endif
    return  0.08 * u_MetallicRoughnessSpecularFactor;
*/
    return 0.08;
}

MaterialInfo getMetallicRoughnessInfo(Material material, MaterialInfo info, float f0_ior)
{
    info.metallic = material.metallicFactor;
    info.perceptualRoughness = material.roughnessFactor;

    if (material.physicalDescriptorTextureSet > -1) {
        // Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
        // This layout intentionally reserves the 'r' channel for (optional) occlusion map data
        vec4 mrSample = texture(physicalDescriptorMap, material.physicalDescriptorTextureSet == 0 ? inUV0 : inUV1);
        info.perceptualRoughness *= mrSample.g;
        info.metallic *= mrSample.b;
    }

    // @todo
/*
#ifdef MATERIAL_METALLICROUGHNESS_SPECULAROVERRIDE
    // Overriding the f0 creates unrealistic materials if the IOR does not match up.
    vec3 f0 = vec3(getMetallicRoughnessSpecularFactor());
#else
    // Achromatic f0 based on IOR.
    vec3 f0 = vec3(f0_ior);
#endif
*/
    vec3 f0 = vec3(f0_ior);

    info.albedoColor = mix(info.baseColor.rgb * (vec3(1.0) - f0),  vec3(0), info.metallic);
    info.f0 = mix(f0, info.baseColor.rgb, info.metallic);

    return info;
}

MaterialInfo getSheenInfo(Material material, MaterialInfo info)
{
    info.sheenColor = material.sheenColorFactorAndRoughness.rgb;
    // @todo
//    info.sheenIntensity = u_SheenIntensityFactor;
    info.sheenIntensity = 1.0;
    info.sheenRoughness = material.sheenColorFactorAndRoughness.a;

    // @todo
//    #ifdef HAS_SHEEN_COLOR_INTENSITY_MAP
//        vec4 sheenSample = texture(u_SheenColorIntensitySampler, getSheenUV());
//        info.sheenColor *= sheenSample.xyz;
//        info.sheenIntensity *= sheenSample.w;
//    #endif
//
    return info;
}

#ifdef MATERIAL_SUBSURFACE
MaterialInfo getSubsurfaceInfo(MaterialInfo info)
{
    info.subsurfaceScale = u_SubsurfaceScale;
    info.subsurfaceDistortion = u_SubsurfaceDistortion;
    info.subsurfacePower = u_SubsurfacePower;
    info.subsurfaceColor = u_SubsurfaceColorFactor;
    info.subsurfaceThickness = u_SubsurfaceThicknessFactor;

    #ifdef HAS_SUBSURFACE_COLOR_MAP
        info.subsurfaceColor *= texture(u_SubsurfaceColorSampler, getSubsurfaceColorUV()).rgb;
    #endif

    #ifdef HAS_SUBSURFACE_THICKNESS_MAP
        info.subsurfaceThickness *= texture(u_SubsurfaceThicknessSampler, getSubsurfaceThicknessUV()).r;
    #endif

    return info;
}
#endif

// @todo
/*
vec3 getThinFilmF0(vec3 f0, vec3 f90, float NdotV, float thinFilmFactor, float thinFilmThickness)
{
    if (thinFilmFactor == 0.0)
    {
        // No thin film applied.
        return f0;
    }

    vec3 lutSample = texture(u_ThinFilmLUT, vec2(thinFilmThickness, NdotV)).rgb - 0.5;
    vec3 intensity = thinFilmFactor * 4.0 * f0 * (1.0 - f0);
    return clamp(intensity * lutSample, 0.0, 1.0);
}

#ifdef MATERIAL_THIN_FILM
MaterialInfo getThinFilmInfo(MaterialInfo info)
{
    info.thinFilmFactor = u_ThinFilmFactor;
    info.thinFilmThickness = u_ThinFilmThicknessMaximum / 1200.0;

    #ifdef HAS_THIN_FILM_MAP
        info.thinFilmFactor *= texture(u_ThinFilmSampler, getThinFilmUV()).r;
    #endif

    #ifdef HAS_THIN_FILM_THICKNESS_MAP
        float thicknessSampled = texture(u_ThinFilmThicknessSampler, getThinFilmThicknessUV()).g;
        float thickness = mix(u_ThinFilmThicknessMinimum / 1200.0, u_ThinFilmThicknessMaximum / 1200.0, thicknessSampled);
        info.thinFilmThickness = thickness;
    #endif

    return info;
}
#endif

MaterialInfo getTransmissionInfo(MaterialInfo info)
{
    info.transmission = u_Transmission;
    return info;
}

MaterialInfo getThicknessInfo(MaterialInfo info)
{
    info.thickness = 1.0;

    #ifdef MATERIAL_THICKNESS
    info.thickness = u_Thickness;

    #ifdef HAS_THICKNESS_MAP
    info.thickness *= texture(u_ThicknessSampler, getThicknessUV()).r;
    #endif

    #endif

    return info;
}

MaterialInfo getAbsorptionInfo(MaterialInfo info)
{
    info.absorption = vec3(0.0);

    #ifdef MATERIAL_ABSORPTION
    info.absorption = u_AbsorptionColor;
    #endif

    return info;
}

MaterialInfo getAnisotropyInfo(MaterialInfo info)
{
    info.anisotropy = u_Anisotropy;

#ifdef HAS_ANISOTROPY_MAP
    info.anisotropy *= texture(u_AnisotropySampler, getAnisotropyUV()).r * 2.0 - 1.0;
#endif

    return info;
}
*/

MaterialInfo getClearCoatInfo(Material material, MaterialInfo info, NormalInfo normalInfo)
{
    info.clearcoatFactor = material.clearcoatFactorAndRoughness.r;
    info.clearcoatRoughness = material.clearcoatFactorAndRoughness.g;
    info.clearcoatF0 = vec3(0.04);
    info.clearcoatF90 = vec3(clamp(info.clearcoatF0 * 50.0, 0.0, 1.0));
    
    if (material.clearcoatTextureSet > -1) {
        vec4 ccSample = texture(clearcoatMap, material.clearcoatTextureSet == 0 ? inUV0 : inUV1);
        info.clearcoatFactor *= ccSample.r;
    }

    if (material.clearcoatRoughnessTextureSet > -1) {
        vec4 ccSampleRough = texture(clearcoatRoughnessMap, material.clearcoatRoughnessTextureSet == 0 ? inUV0 : inUV1);
        info.clearcoatRoughness *= ccSampleRough.g;
    }

    // @todo
    if (material.clearcoatNormalTextureSet > -1) {
        vec4 ccSampleNor = texture(clearcoatNormalMap, material.clearcoatNormalTextureSet == 0 ? inUV0 : inUV1);
        info.clearcoatNormal = normalize(ccSampleNor.xyz);
    } else {
        info.clearcoatNormal = normalInfo.ng;
    }

    info.clearcoatRoughness = clamp(info.clearcoatRoughness, 0.0, 1.0);

    return info;
}

void main()
{
    // Fetch material for this primitive from the material buffer
    Material material = materials[pushConstant.materialIndex];

    vec4 baseColor = getBaseColor(material);

#ifdef ALPHAMODE_OPAQUE
    baseColor.a = 1.0;
#endif

#ifdef MATERIAL_UNLIT
    finalColor = (vec4(linearTosRGB(baseColor.rgb), baseColor.a));
    return;
#endif

    vec3 v = normalize(ubo.camPos - inWorldPos);
    NormalInfo normalInfo = getNormalInfo(material, v);
    vec3 n = normalInfo.n;
    vec3 t = normalInfo.t;
    vec3 b = normalInfo.b;

    float NdotV = clampedDot(n, v);
    float TdotV = clampedDot(t, v);
    float BdotV = clampedDot(b, v);

    MaterialInfo materialInfo;
    materialInfo.baseColor = baseColor.rgb;

#ifdef MATERIAL_IOR
    float ior = u_IOR_and_f0.x;
    float f0_ior = u_IOR_and_f0.y;
#else
    // The default index of refraction of 1.5 yields a dielectric normal incidence reflectance of 0.04.
    float ior = 1.5;
    float f0_ior = 0.04;
#endif

    if (material.hasMetallicRoughness == 1) {
        materialInfo = getMetallicRoughnessInfo(material, materialInfo, f0_ior);
    }
  
  // @todo
//    if (material.hasSpecularGlossiness == 1) {
//        materialInfo = getSpecularGlossinessInfo(material, materialInfo);
//    }
//

#ifdef MATERIAL_SHEEN
    materialInfo = getSheenInfo(materialInfo);
#endif

#ifdef MATERIAL_SUBSURFACE
    materialInfo = getSubsurfaceInfo(materialInfo);
#endif

#ifdef MATERIAL_THIN_FILM
    materialInfo = getThinFilmInfo(materialInfo);
#endif

    if (material.hasClearcoat == 1) {
        materialInfo = getClearCoatInfo(material, materialInfo, normalInfo);
    }

#ifdef MATERIAL_TRANSMISSION
    materialInfo = getTransmissionInfo(materialInfo);
#endif

#ifdef MATERIAL_ANISOTROPY
    materialInfo = getAnisotropyInfo(materialInfo);
#endif

    // @todo: implement
//    materialInfo = getThicknessInfo(materialInfo);
//    materialInfo = getAbsorptionInfo(materialInfo);
//
    materialInfo.perceptualRoughness = clamp(materialInfo.perceptualRoughness, 0.0, 1.0);
    materialInfo.metallic = clamp(materialInfo.metallic, 0.0, 1.0);

    // Roughness is authored as perceptual roughness; as is convention,
    // convert to material roughness by squaring the perceptual roughness.
    materialInfo.alphaRoughness = materialInfo.perceptualRoughness * materialInfo.perceptualRoughness;

    // Compute reflectance.
    float reflectance = max(max(materialInfo.f0.r, materialInfo.f0.g), materialInfo.f0.b);

    // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
    materialInfo.f90 = vec3(clamp(reflectance * 50.0, 0.0, 1.0));

    materialInfo.n = n;

#ifdef MATERIAL_THIN_FILM
    materialInfo.f0 = getThinFilmF0(materialInfo.f0, materialInfo.f90, clampedDot(n, v),
        materialInfo.thinFilmFactor, materialInfo.thinFilmThickness);
#endif

    // LIGHTING
    vec3 f_specular = vec3(0.0);
    vec3 f_diffuse = vec3(0.0);
    vec3 f_emissive = vec3(0.0);
    vec3 f_clearcoat = vec3(0.0);
    vec3 f_sheen = vec3(0.0);
    vec3 f_subsurface = vec3(0.0);
    vec3 f_transmission = vec3(0.0);

    // Calculate lighting contribution from image based lighting source (IBL)
    // @todo: via param

    f_specular += getIBLRadianceGGX(envGGX, lutGGX, uboParams.prefilteredCubeMipLevels, n, v, materialInfo.perceptualRoughness, materialInfo.f0);
    f_diffuse += getIBLRadianceLambertian(envLambertian, n, materialInfo.albedoColor);

    if (material.hasClearcoat == 1) {
        f_clearcoat += getIBLRadianceGGX(envGGX, lutGGX, uboParams.prefilteredCubeMipLevels, materialInfo.clearcoatNormal, v, materialInfo.clearcoatRoughness, materialInfo.clearcoatF0);
    }

    #ifdef MATERIAL_SHEEN
        f_sheen += getIBLRadianceCharlie(n, v, materialInfo.sheenRoughness, materialInfo.sheenColor, materialInfo.sheenIntensity);
    #endif

    #ifdef MATERIAL_SUBSURFACE
        f_subsurface += getIBLRadianceSubsurface(n, v, materialInfo.subsurfaceScale, materialInfo.subsurfaceDistortion, materialInfo.subsurfacePower, materialInfo.subsurfaceColor, materialInfo.subsurfaceThickness);
    #endif

    #ifdef MATERIAL_TRANSMISSION
        f_transmission += getIBLRadianceTransmission(n, v, materialInfo.perceptualRoughness, ior, materialInfo.baseColor);
    #endif

#ifdef USE_PUNCTUAL
    for (int i = 0; i < LIGHT_COUNT; ++i)
    {
        Light light = u_Lights[i];

        vec3 pointToLight = -light.direction;
        float rangeAttenuation = 1.0;
        float spotAttenuation = 1.0;

        if(light.type != LightType_Directional)
        {
            pointToLight = light.position - inWorldPos;
        }

        // Compute range and spot light attenuation.
        if (light.type != LightType_Directional)
        {
            rangeAttenuation = getRangeAttenuation(light.range, length(pointToLight));
        }
        if (light.type == LightType_Spot)
        {
            spotAttenuation = getSpotAttenuation(pointToLight, light.direction, light.outerConeCos, light.innerConeCos);
        }

        vec3 intensity = rangeAttenuation * spotAttenuation * light.intensity * light.color;

        vec3 l = normalize(pointToLight);   // Direction from surface point to light
        vec3 h = normalize(l + v);          // Direction of the vector between l and v, called halfway vector
        float NdotL = clampedDot(n, l);
        float NdotV = clampedDot(n, v);
        float NdotH = clampedDot(n, h);
        float LdotH = clampedDot(l, h);
        float VdotH = clampedDot(v, h);

        if (NdotL > 0.0 || NdotV > 0.0)
        {
            // Calculation of analytical light
            //https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#acknowledgments AppendixB
            f_diffuse += intensity * NdotL *  BRDF_lambertian(materialInfo.f0, materialInfo.f90, materialInfo.albedoColor, VdotH);

            #ifdef MATERIAL_ANISOTROPY
            vec3 h = normalize(l + v);
            float TdotL = dot(t, l);
            float BdotL = dot(b, l);
            float TdotH = dot(t, h);
            float BdotH = dot(b, h);
            f_specular += intensity * NdotL * BRDF_specularAnisotropicGGX(materialInfo.f0, materialInfo.f90, materialInfo.alphaRoughness,
                VdotH, NdotL, NdotV, NdotH,
                BdotV, TdotV, TdotL, BdotL, TdotH, BdotH, materialInfo.anisotropy);
            #else
            f_specular += intensity * NdotL * BRDF_specularGGX(materialInfo.f0, materialInfo.f90, materialInfo.alphaRoughness, VdotH, NdotL, NdotV, NdotH);
            #endif

            #ifdef MATERIAL_SHEEN
                f_sheen += intensity * getPunctualRadianceSheen(materialInfo.sheenColor, materialInfo.sheenIntensity, materialInfo.sheenRoughness,
                    NdotL, NdotV, NdotH);
            #endif

            #ifdef MATERIAL_CLEARCOAT
                f_clearcoat += intensity * getPunctualRadianceClearCoat(materialInfo.clearcoatNormal, v, l,
                    h, VdotH,
                    materialInfo.clearcoatF0, materialInfo.clearcoatF90, materialInfo.clearcoatRoughness);
            #endif
        }

        #ifdef MATERIAL_SUBSURFACE
            f_subsurface += intensity * getPunctualRadianceSubsurface(n, v, l,
                materialInfo.subsurfaceScale, materialInfo.subsurfaceDistortion, materialInfo.subsurfacePower,
                materialInfo.subsurfaceColor, materialInfo.subsurfaceThickness);
        #endif

        #ifdef MATERIAL_TRANSMISSION
            f_transmission += intensity * getPunctualRadianceTransmission(n, v, l, materialInfo.alphaRoughness, ior, materialInfo.f0);
        #endif
    }
#endif // !USE_PUNCTUAL

    f_emissive = material.emissiveFactor.xyz;
	if (material.emissiveTextureSet > -1) {
        f_emissive *= sRGBToLinear(texture(emissiveMap, material.emissiveTextureSet == 0 ? inUV0 : inUV1)).rgb;
    }

    vec3 color = vec3(0);

///
/// Layer blending
///

    float clearcoatFactor = 0.0;
    vec3 clearcoatFresnel = vec3(0.0);

    if (material.hasClearcoat == 1) {
        clearcoatFactor = materialInfo.clearcoatFactor;
        clearcoatFresnel = F_Schlick(materialInfo.clearcoatF0, materialInfo.clearcoatF90, clampedDot(materialInfo.clearcoatNormal, v));
    }

    #ifdef MATERIAL_ABSORPTION
        f_transmission *= transmissionAbsorption(v, n, ior, materialInfo.thickness, materialInfo.absorption);
    #endif

    #ifdef MATERIAL_TRANSMISSION
    vec3 diffuse = mix(f_diffuse, f_transmission, materialInfo.transmission);
    #else
    vec3 diffuse = f_diffuse;
    #endif

    color = (f_emissive + diffuse + f_specular + f_subsurface + (1.0 - reflectance) * f_sheen) * (1.0 - clearcoatFactor * clearcoatFresnel) + f_clearcoat * clearcoatFactor;

    float ao = 1.0;
    // Apply optional PBR terms for additional (optional) shading
	if (material.occlusionTextureSet > -1) {
        ao = texture(aoMap, material.occlusionTextureSet == 0 ? inUV0 : inUV1).r;
        // @todo
        float u_OcclusionStrength = 1.0;
        color = mix(color, color * ao, u_OcclusionStrength);
    }

    if (material.alphaMask == 1.0) {
        // Late discard to avoid samplig artifacts. See https://github.com/KhronosGroup/glTF-Sample-Viewer/issues/267
        if(baseColor.a < material.alphaMaskCutoff)
        {
            discard;
        }
        baseColor.a = 1.0;
    }

    // regular shading
    finalColor = vec4(toneMap(uboParams.toneMapper, color, uboParams.exposure), baseColor.a);

    if (uboParams.debugViewInputs > 0) {
        switch(int(uboParams.debugViewInputs)) {
            case 1:
                finalColor.rgb = vec3(materialInfo.metallic);
                break;
            case 2: 
                finalColor.rgb = vec3(materialInfo.perceptualRoughness);
                break;
            case 3:
                if (material.normalTextureSet > -1) {
                    finalColor.rgb = texture(normalMap, material.normalTextureSet == 0 ? inUV0 : inUV1).rgb;// * 2.0 - 1.0;
                } else {
                    finalColor.rgb = vec3(0.5, 0.5, 1.0);;
                }
                break;
            case 4: 
                finalColor.rgb = t * 0.5 + vec3(0.5);
                break;
            case 5:
                finalColor.rgb = b * 0.5 + vec3(0.5);
                break;
            case 6:
                finalColor.rgb = linearTosRGB(materialInfo.baseColor);
                break;
            case 7:
                finalColor.rgb = vec3(ao);
                break;
            case 8:
                finalColor.rgb = f_emissive;
                break;
            case 9:
                finalColor.rgb = f_diffuse;
                break;
            case 10:
                finalColor.rgb = f_specular;
                break;
            case 11: 
                finalColor.rgb = vec3(materialInfo.thickness);
                break;
            case 12:
                finalColor.rgb = f_clearcoat;
                break;
            case 13:
                finalColor.rgb = f_sheen;
                break;
            case 14:
                finalColor.rgb = f_subsurface;
                break;
            case 15:               
                finalColor.rgb = linearTosRGB(f_transmission);
                break;
            case 16:
                finalColor.rgb = vec3(baseColor.a);
                break;
            case 17:
                finalColor.rgb = materialInfo.f0;
                break;
        }
    }
}
