// This is the same algorithm as found in libraries\pbrlib\genglsl\lib\mx_environment_fis.glsl
// but adjusted for Maya.
//
// Since we are on a more recent versions of Maya, we have external lighting functions that can be
// called to fetch environment samples:
#include "libraries/pbrlib/genglsl/lib/mx_microfacet_specular.glsl"

// https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch20.html
// Section 20.4 Equation 13
float mx_latlong_compute_lod_adsk(vec3 dir, float pdf, float maxMipLevel, int envSamples)
{
    const float MIP_LEVEL_OFFSET = 1.5;
    float effectiveMaxMipLevel = maxMipLevel - MIP_LEVEL_OFFSET;
    float distortion = sqrt(1.0 - mx_square(dir.y));
    return max(effectiveMaxMipLevel - 0.5 * log2(float(envSamples) * pdf * distortion), 0.0);
}

vec3 mx_environment_radiance(vec3 N, vec3 V, vec3 X, vec2 alpha, int distribution, FresnelData fd)
{
    if (mayaGetSpecularEnvironmentNumLOD() == 0) {
        return vec3(0);
    }
    
    // Generate tangent frame.
    vec3 Y = normalize(cross(N, X));
    X = cross(Y, N);
    mat3 tangentToWorld = mat3(X, Y, N);

    // Transform the view vector to tangent space.
    V = vec3(dot(V, X), dot(V, Y), dot(V, N));

    // Compute derived properties.
    float NdotV = clamp(V.z, M_FLOAT_EPS, 1.0);
    float avgAlpha = mx_average_alpha(alpha);
    
    // Integrate outgoing radiance using filtered importance sampling.
    // http://cgg.mff.cuni.cz/~jaroslav/papers/2008-egsr-fis/2008-egsr-fis-final-embedded.pdf
    vec3 radiance = vec3(0.0);
    for (int i = 0; i < MX_NUM_FIS_SAMPLES; i++)
    {
        vec2 Xi = mx_spherical_fibonacci(i, MX_NUM_FIS_SAMPLES);

        // Compute the half vector and incoming light direction.
        vec3 H = mx_ggx_importance_sample_NDF(Xi, alpha);
        vec3 L = -reflect(V, H);
        
        // Compute dot products for this sample.
        float NdotH = clamp(H.z, M_FLOAT_EPS, 1.0);
        float NdotL = clamp(L.z, M_FLOAT_EPS, 1.0);
        float VdotH = clamp(dot(V, H), M_FLOAT_EPS, 1.0);
        float LdotH = VdotH;

        // Sample the environment light from the given direction.
        vec3 Lw = tangentToWorld * L;
        float pdf = mx_ggx_PDF(H, LdotH, alpha);
        float lod = mx_latlong_compute_lod_adsk(Lw, pdf, float(mayaGetSpecularEnvironmentNumLOD() - 1), MX_NUM_FIS_SAMPLES);
        vec3 sampleColor = mayaSampleSpecularEnvironmentAtLOD(Lw, lod);

        // Compute the Fresnel term.
        vec3 F = mx_compute_fresnel(VdotH, fd);

        // Compute the geometric term.
        float G = mx_ggx_smith_G2(NdotL, NdotV, avgAlpha);

        // Add the radiance contribution of this sample.
        // From https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
        //   incidentLight = sampleColor * NdotL
        //   microfacetSpecular = D * F * G / (4 * NdotL * NdotV)
        //   pdf = D * NdotH / (4 * VdotH)
        //   radiance = incidentLight * microfacetSpecular / pdf
        radiance += sampleColor * F * G * VdotH / (NdotV * NdotH);
    }

    // Normalize and return the final radiance.
    radiance /= float(MX_NUM_FIS_SAMPLES);
    return radiance;
}

vec3 mx_environment_irradiance(vec3 N)
{
    return mayaGetIrradianceEnvironment(N);
}
