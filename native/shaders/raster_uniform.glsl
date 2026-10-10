#ifndef K22_RASTER_UNIFORM_GLSL
#define K22_RASTER_UNIFORM_GLSL
#ifndef K22_RAY_QUERY
#define K22_RAY_QUERY 0
#endif
layout(set=0,binding=0,std140) uniform Uniform {
    mat4 dataModel,dataViewProjection,dataNormalMatrix,dataPortalProjection,dataProjection;
    vec4 dataClipPlane,dataPortalViewport,dataPortalPlane,dataTint,dataSunDirection,dataCameraPosition,dataFogColor;
    vec4 dataUvTransform,dataFogParameters,dataNormalUv,dataSkillMain,dataSkillExtra;
    vec4 dataPreparedAmbient,dataPreparedDiffuse,dataPreparedSpecular,dataPreparedEmissive,dataAmbientLight;
    vec4 dataViewport,dataSkyConeScroll,dataSkyParameters;
    ivec4 dataFlags[4];
    vec4 dataParameters[3];
    vec4 dataPbr; // x PBR on, y tone map in shader (LDR views), z pack roughness present, w legacy roughness.
    // Wave-2 effects (zero = original path).
    mat4 dataSunShadow,dataInverseViewProjection,dataPreviousViewProjection;
    vec4 dataShadowLight; // xyz cube shadow light position, w its far plane (range); 0 = no cube shadow.
    vec4 dataEffects; // x shadow maps on, y RT bits (1 shadows, 2 GI, 4 reflections signals of the primary view; 8 shadows=rt in every view), z sun shadow texel (world), w cube shadow light index.
    vec4 dataPass; // x pass (0 scene, 1 shadow caster, 2 G-buffer; effect passes: effect mode), y frame seed, zw texel size.
    vec4 dataCamera; // xyz camera forward, w near plane.
    vec4 dataRange; // x far plane, yz fog start/end, w effect parameter.
    mat4 dataPreviousModel; // Motion-vector pass: the entity's previous-frame model transform.
} u;
#define k22ShadowMaps (u.dataEffects.x!=0)
#define k22RtSignals int(u.dataEffects.y)
#define k22RtShadowMode ((k22RtSignals&8)!=0)
#define k22Pass int(u.dataPass.x)
float rayMaxDistance(){return u.dataCameraPosition.w;}
#define k22PbrOn (u.dataPbr.x!=0)
#define k22ToneInShader (u.dataPbr.y!=0)
#define k22RoughnessMap (u.dataPbr.z!=0)
#define k22LegacyRoughness u.dataPbr.w
#define model u.dataModel
#define viewProjection u.dataViewProjection
#define normalMatrix mat3(u.dataNormalMatrix)
#define portalProjection u.dataPortalProjection
#define projection u.dataProjection
#define clipPlane u.dataClipPlane
#define portalViewport u.dataPortalViewport
#define portalPlane u.dataPortalPlane
#define tint u.dataTint
#define sunDirection u.dataSunDirection.xyz
#define cameraPosition u.dataCameraPosition.xyz
#define fogColor u.dataFogColor.xyz
#define uvScale u.dataUvTransform.xy
#define uvOffset u.dataUvTransform.zw
#define fogRange u.dataFogParameters.xy
#define alpha u.dataFogParameters.z
#define materialAmbient u.dataFogParameters.w
#define normalUvScale u.dataNormalUv.xy
#define surfaceAmbient u.dataNormalUv.z
#define normalOrientation u.dataNormalUv.w
#define skillMain u.dataSkillMain
#define skillExtra u.dataSkillExtra
#define preparedAmbient u.dataPreparedAmbient
#define preparedDiffuse u.dataPreparedDiffuse.xyz
#define preparedSpecular u.dataPreparedSpecular.xyz
#define preparedEmissive u.dataPreparedEmissive.xyz
#define materialPower u.dataPreparedSpecular.w
#define ambientLight u.dataAmbientLight.xyz
#define materialSunStrength u.dataAmbientLight.w
#define selectedLight int(u.dataPreparedDiffuse.w)
#define infiniteViewer u.dataParameters[2].yzw
#define viewport u.dataViewport.xy
#define imageSize ivec2(u.dataViewport.zw)
#define cone u.dataSkyConeScroll.xy
#define scroll u.dataSkyConeScroll.zw
#define cube (u.dataSkyParameters.x!=0)
#define blend (u.dataSkyParameters.y!=0)
#define curve u.dataSkyParameters.z
#define clip u.dataSkyParameters.w
#define lightCount u.dataFlags[0].x
#define spriteFrames u.dataFlags[0].y
#define frame0 u.dataFlags[0].z
#define frame1 u.dataFlags[0].w
#define hasLightmap (u.dataFlags[1].x!=0)
#define unlit (u.dataFlags[1].y!=0)
#define flare (u.dataFlags[1].z!=0)
#define overlay (u.dataFlags[1].w!=0)
#define invertFlare (u.dataFlags[2].x!=0)
#define textureAlpha (u.dataFlags[2].y!=0)
#define noFog (u.dataFlags[2].z!=0)
#define skyMask (u.dataFlags[2].w!=0)
#define effectKind u.dataFlags[3].x
#define explicitMaterial (u.dataFlags[3].y!=0)
#define worldVertexLighting (u.dataFlags[3].z!=0)
#define portalEnabled (u.dataFlags[3].w!=0)
#define reflectionWeight u.dataParameters[0].x
#define interpolation u.dataParameters[0].y
#define areaThreshold u.dataParameters[0].z
#define stride int(u.dataParameters[0].z)
#define alphaRef u.dataParameters[0].w
#define uiOverlay (u.dataParameters[1].x!=0)
#define textKey (u.dataParameters[1].y!=0)
#define cursorKey (u.dataParameters[1].z!=0)
#define enabled (u.dataParameters[1].w!=0)
#define reversed (u.dataParameters[2].x!=0)
#define layer int(u.dataParameters[2].y)
#define level int(u.dataParameters[2].z)
#define convertAlpha (u.dataParameters[2].w!=0)
#endif
