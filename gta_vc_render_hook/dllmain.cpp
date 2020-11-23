#include <DebugUtils/DebugLogger.h>
#include <Engine/Common/IDeviceState.h>
#include <MemoryInjectionUtils/InjectorHelpers.h>
#include <Windows.h>
#include <filesystem>
#include <ipc/ipc_utils.h>
#include <render_loop.h>
#include <rw_engine/rh_backend/material_backend.h>
#include <rw_engine/rh_backend/raster_backend.h>
#include <rw_engine/rw_api_injectors.h>
#include <rw_engine/rw_frame/rw_frame.h>
#include <rw_engine/rw_macro_constexpr.h>
#include <rw_engine/rw_rh_pipeline.h>
#include <rw_engine/rw_rh_skin_pipeline.h>
#include <rw_engine/system_funcs/rw_device_system_globals.h>
#include <rw_game_hooks.h>

using namespace rh::rw::engine;

RwIOPointerTable rh::rw::engine::g_pIO_API = {
    reinterpret_cast<RwStreamFindChunk_FN>( 0x64FAC0 ),
    reinterpret_cast<RwStreamRead_FN>( 0x6454B0 ) };
RwRasterPointerTable rh::rw::engine::g_pRaster_API = {
    reinterpret_cast<RwRasterCreate_FN>( 0x655490 ),
    reinterpret_cast<RwRasterDestroy_FN>( 0x6552E0 ) };
RwTexturePointerTable rh::rw::engine::g_pTexture_API = {
    reinterpret_cast<RwTextureCreate_FN>( 0x64DE60 ),
    reinterpret_cast<RwTextureSetName_FN>( 0x64DF40 ),
    reinterpret_cast<RwTextureSetName_FN>( 0x64DFB0 ) };

struct RwVideoModeVice
{
    int32_t         width;
    int32_t         height;
    int32_t         depth;
    RwVideoModeFlag flags;
};

RwVideoModeVice *MyRwEngineGetVideoModeInfo( RwVideoModeVice *modeinfo,
                                             int32_t          modeIndex )
{

    RwVideoMode videoMode{};
    if ( !SystemHandler( rwDEVICESYSTEMGETMODEINFO, &videoMode, nullptr,
                         modeIndex ) )
        return nullptr;
    modeinfo->width  = videoMode.width;
    modeinfo->height = videoMode.height;
    modeinfo->depth  = videoMode.depth;
    modeinfo->flags  = videoMode.flags;
    return modeinfo;
}

struct RxPipelineNodeParam
{
    void *                 dataParam;
    [[maybe_unused]] void *heap;
};

static int32_t D3D8AtomicAllInOneNode( void * /*self*/,
                                       const RxPipelineNodeParam *params )
{
    static RpGeometryRw36 geometry_interface_35{};
    RpAtomic *            atomic;

    atomic                   = static_cast<RpAtomic *>( params->dataParam );
    RpGeometry *  geom       = atomic->geometry;
    RpMeshHeader *meshHeader = geom->mesh;
    geometry_interface_35.Init( geom );
    // rt_scene->PushModel( meshHeader->serialNum, &geometry_interface_35 );
    if ( RwRHInstanceAtomic( atomic, &geometry_interface_35 ) !=
         RenderStatus::Instanced )
        return 0;

    auto ltm = rh::rw::engine::RwFrameGetLTM( static_cast<RwFrame *>(
        rh::rw::engine::rwObject::GetParent( atomic ) ) );
    rh::rw::engine::DrawAtomic(
        atomic, &geometry_interface_35,
        [&ltm, atomic]( rh::rw::engine::ResEnty *res_entry ) {
            rh::rw::engine::DrawCallInfo info{};
            info.mDrawCallId     = reinterpret_cast<uint64_t>( atomic );
            info.mMeshId         = res_entry->meshData;
            info.mWorldTransform = DirectX::XMFLOAT4X3{
                ltm->right.x, ltm->up.x, ltm->at.x, ltm->pos.x,
                ltm->right.y, ltm->up.y, ltm->at.y, ltm->pos.y,
                ltm->right.z, ltm->up.z, ltm->at.z, ltm->pos.z,
            };
            std::vector<rh::rw::engine::MaterialData> materials{};
            auto        meshHeader = geometry_interface_35.GetMeshHeader();
            const auto *mesh_start =
                reinterpret_cast<const RpMesh *>( meshHeader + 1 );
            for ( const RpMesh *mesh = mesh_start;
                  mesh != mesh_start + meshHeader->numMeshes; mesh++ )
            {
                auto m   = mesh->material;
                auto m_b = GetBackendMaterialExt( m );

                auto    spec_tex    = m_b->mSpecTex;
                int32_t tex_id      = 0xBADF00D;
                int32_t spec_tex_id = 0xBADF00D;
                if ( m->texture && m->texture->raster )
                {
                    auto raster = GetBackendRasterExt( m->texture->raster );
                    assert( raster->mImageId <=
                            ( std::numeric_limits<int32_t>::max )() );
                    tex_id = raster->mImageId;
                }
                if ( spec_tex && spec_tex->raster )
                {
                    auto raster = GetBackendRasterExt( spec_tex->raster );
                    assert( raster->mImageId <=
                            ( std::numeric_limits<int32_t>::max )() );
                    spec_tex_id = raster->mImageId;
                }
                materials.push_back( rh::rw::engine::MaterialData{
                    tex_id, m->color, spec_tex_id, m->surfaceProps.specular } );
            }
            rh::rw::engine::EngineClient::gRendererGlobals.RecordDrawCall(
                info, materials );
        } );
    return 1;
}

static int32_t D3D8SkinAtomicAllInOneNode( void * /*self*/,
                                           const RxPipelineNodeParam *params )
{
    static RpGeometryRw36 geometry_interface_35{};
    RpAtomic *            atomic;

    atomic           = static_cast<RpAtomic *>( params->dataParam );
    RpGeometry *geom = atomic->geometry;
    // RpMeshHeader *meshHeader = geom->mesh;

    geometry_interface_35.Init( geom );
    if ( RwRHInstanceSkinAtomic( atomic, &geometry_interface_35 ) !=
         RenderStatus::Instanced )
        return 0;

    auto ltm = rh::rw::engine::RwFrameGetLTM( static_cast<RwFrame *>(
        rh::rw::engine::rwObject::GetParent( atomic ) ) );
    rh::rw::engine::DrawAtomic(
        atomic, &geometry_interface_35,
        [&ltm, atomic]( rh::rw::engine::ResEnty *res_entry ) {
            rh::rw::engine::SkinDrawCallInfo info{};
            info.mSkinId         = reinterpret_cast<uint64_t>( atomic );
            info.mMeshId         = res_entry->meshData;
            info.mWorldTransform = DirectX::XMFLOAT4X3{
                ltm->right.x, ltm->up.x, ltm->at.x, ltm->pos.x,
                ltm->right.y, ltm->up.y, ltm->at.y, ltm->pos.y,
                ltm->right.z, ltm->up.z, ltm->at.z, ltm->pos.z,
            };
            std::vector<rh::rw::engine::MaterialData> materials{};
            auto        meshHeader = geometry_interface_35.GetMeshHeader();
            const auto *mesh_start =
                reinterpret_cast<const RpMesh *>( meshHeader + 1 );
            for ( const RpMesh *mesh = mesh_start;
                  mesh != mesh_start + meshHeader->numMeshes; mesh++ )
            {
                auto m = mesh->material;

                int32_t tex_id      = 0xBADF00D;
                int32_t spec_tex_id = 0xBADF00D;
                if ( m->texture && m->texture->raster )
                {
                    auto raster = GetBackendRasterExt( m->texture->raster );
                    tex_id      = raster->mImageId;
                }
                materials.push_back( rh::rw::engine::MaterialData{
                    tex_id, m->color, spec_tex_id, 0.0f } );
            }
            static rh::rw::engine::AnimHierarcyRw36 g_anim{};
            rh::rw::engine::PrepareBoneMatrices( info.mBoneTransform, atomic,
                                                 g_anim );
            rh::rw::engine::EngineClient::gSkinRendererGlobals.RecordDrawCall(
                info, materials );
        } );
    /*DirectX::XMMATRIX objTransformMatrix = {
        ltm->right.x, ltm->right.y, ltm->right.z, 0,
        ltm->up.x,    ltm->up.y,    ltm->up.z,    0,
        ltm->at.x,    ltm->at.y,    ltm->at.z,    0,
        ltm->pos.x,   ltm->pos.y,   ltm->pos.z,   1 };
    struct PerModelCB
    {
        DirectX::XMMATRIX worldMatrix;
        DirectX::XMMATRIX worldITMatrix;
    } perModelCB;
    perModelCB.worldMatrix = objTransformMatrix;
    perModelCB.worldITMatrix =
        DirectX::XMMatrixInverse( nullptr, objTransformMatrix );*/
    // g_pRwRenderEngine->RenderStateSet( rwRENDERSTATEVERTEXALPHAENABLE, 1 );

    /*context->UpdateBuffer( gBaseConstantBuffer, g_cameraContext, sizeof(
*g_cameraContext ) ); context->UpdateBuffer( gPerModelConstantBuffer,
&perModelCB, sizeof( perModelCB ) ); context->BindConstantBuffers(
RHEngine::RHShaderStage::Vertex | RHEngine::RHShaderStage::Pixel |
RHEngine::RHShaderStage::Compute,
                  {{0, gBaseConstantBuffer}, {1,
gPerModelConstantBuffer}} );

DrawAtomic( atomic, &geometry_interface_35, context, g_gbPipeline );*/

    return 1;
}
int32_t true_hook() { return 1; }
struct CVector
{
    float x, y, z;
};
class CPointLight
{
  public:
    CVector       m_vecPosn;
    CVector       m_vecDirection;
    float         m_fRange;
    float         m_fColorRed;
    float         m_fColorGreen;
    float         m_fColorBlue;
    unsigned char m_nType;
    unsigned char m_nFogType;
    bool          m_bGenerateShadows;

  private:
    char _pad2B;

  public:
};

int32_t AddPtLight( char a1, float x, float y, float z, float dx, int dy,
                    int dz, float rad, float r, float g, float b, char fogtype,
                    char extrashadows )
{
    auto &frame_info = rh::rw::engine::GetCurrentSceneGraph()->mFrameInfo;
    if ( frame_info.mLightCount > 1024 )
        return 0;

    auto &l     = frame_info.mFirst4PointLights[frame_info.mLightCount];
    l.mPos[0]   = x;
    l.mPos[1]   = y;
    l.mPos[2]   = z;
    l.mRadius   = rad;
    l.mColor[0] = r;
    l.mColor[1] = g;
    l.mColor[2] = b;
    l.mColor[3] = 1;
    frame_info.mLightCount++;
    return 1;
}

int32_t water_render()
{
    // TODO: MOVE OUT OF HERE

    // Setup timecycle(move out of here)
    auto &frame_info = rh::rw::engine::GetCurrentSceneGraph()->mFrameInfo;
    int & m_nCurrentSkyBottomBlue  = *(int *)0x9B6DF4;
    int & m_nCurrentSkyBottomGreen = *(int *)0x97F208;
    int & m_nCurrentSkyBottomRed   = *(int *)0xA0D958;
    int & m_nCurrentSkyTopBlue     = *(int *)0x978D1C;
    int & m_nCurrentSkyTopGreen    = *(int *)0xA0FD70;
    int & m_nCurrentSkyTopRed      = *(int *)0xA0CE98;

    auto *vec_to_sun_arr   = (RwV3d *)0x792C70;
    int & current_tc_value = *(int *)0xA0CFF8;

    frame_info.mSkyTopColor[0]    = float( m_nCurrentSkyTopRed ) / 255.0f;
    frame_info.mSkyTopColor[1]    = float( m_nCurrentSkyTopGreen ) / 255.0f;
    frame_info.mSkyTopColor[2]    = float( m_nCurrentSkyTopBlue ) / 255.0f;
    frame_info.mSkyTopColor[3]    = 1.0f;
    frame_info.mSkyBottomColor[0] = float( m_nCurrentSkyBottomRed ) / 255.0f;
    frame_info.mSkyBottomColor[1] = float( m_nCurrentSkyBottomGreen ) / 255.0f;
    frame_info.mSkyBottomColor[2] = float( m_nCurrentSkyBottomBlue ) / 255.0f;
    frame_info.mSkyBottomColor[3] = 1.0f;

    frame_info.mSunDir[0] = vec_to_sun_arr[current_tc_value].x;
    frame_info.mSunDir[1] = vec_to_sun_arr[current_tc_value].y;
    frame_info.mSunDir[2] = vec_to_sun_arr[current_tc_value].z;
    frame_info.mSunDir[3] = 1.0f;

    auto * point_lights    = (CPointLight *)0x7E4FE0;
    short &point_light_cnt = *(short *)0x9751B0;

    /*auto non_empty_point_lights = min( point_light_cnt, 32 );
    for ( auto i = 0; i < non_empty_point_lights; i++ )
    {
        frame_info.mFirst4PointLights[i].mPos[0] = point_lights[i].m_vecPosn.x;
        frame_info.mFirst4PointLights[i].mPos[1] = point_lights[i].m_vecPosn.y;
        frame_info.mFirst4PointLights[i].mPos[2] = point_lights[i].m_vecPosn.z;
        frame_info.mFirst4PointLights[i].mRadius = point_lights[i].m_fRange;
    }*/

    for ( auto i = frame_info.mLightCount; i < 1024; i++ )
    {
        frame_info.mFirst4PointLights[i].mPos[0] = 0;
        frame_info.mFirst4PointLights[i].mPos[1] = 0;
        frame_info.mFirst4PointLights[i].mPos[2] = 0;
        frame_info.mFirst4PointLights[i].mRadius = -1;
    }

    std::sort( std::begin( frame_info.mFirst4PointLights ),
               std::end( frame_info.mFirst4PointLights ),
               [&frame_info]( const rh::rw::engine::PointLight &x,
                              const rh::rw::engine::PointLight &y ) {
                   auto dist = []( const rh::rw::engine::PointLight &p,
                                   const DirectX::XMFLOAT4X4 &       viewInv ) {
                       float dir[3];
                       dir[0] = p.mPos[0] - viewInv._14;
                       dir[1] = p.mPos[1] - viewInv._24;
                       dir[2] = p.mPos[2] - viewInv._34;
                       return dir[0] * dir[0] + dir[1] * dir[1] +
                              dir[2] * dir[2];
                   };

                   return ( dist( x, frame_info.mViewInv ) <
                            dist( y, frame_info.mViewInv ) ) &&
                          ( x.mRadius > y.mRadius );
               } );
    return 1;
}

static int32_t rxD3D8SubmitNode( void *, const void * ) { return 1; }
int32_t        RwIm3DRenderLine( int32_t vert1, int32_t vert2 ) { return 1; }
int32_t RwIm3DRenderTriangle( int32_t vert1, int32_t vert2, int32_t vert3 )
{
    return 1;
}
int32_t RwIm3DRenderIndexedPrimitive( RwPrimitiveType primType,
                                      uint16_t *indices, int32_t numIndices )
{
    rh::rw::engine::EngineClient::gIm3DGlobals.RenderIndexedPrimitive(
        primType, indices, numIndices );
    return 1;
}
int32_t RwIm3DRenderPrimitive( RwPrimitiveType primType ) { return 1; }
void *  RwIm3DTransform( void *pVerts, uint32_t numVerts, RwMatrix *ltm,
                         uint32_t flags )
{
    rh::rw::engine::EngineClient::gIm3DGlobals.Transform(
        static_cast<RwIm3DVertex *>( pVerts ), numVerts, ltm, flags );
    return pVerts;
}
void RwIm3DEnd() {}

RpMaterial *RpMaterialStreamReadImpl( void *stream )
{
    return ( reinterpret_cast<RpMaterial *(__cdecl *)( void * )>( 0x655920 ) )(
        stream );
}

RwTexture *RwTextureRead( const char *name, const char *maskName )
{
    return (
        reinterpret_cast<RwTexture *(__cdecl *)( const char *, const char * )>(
            0x64E110 ) )( name, maskName );
}

std::unordered_map<std::string, std::string> g_specular_storage{};

void InitSpecStorage()
{
    static bool is_initialized = false;
    if ( is_initialized )
        return;
    namespace fs  = std::filesystem;
    auto dir_path = fs::current_path() / "materials";
    if ( !fs::exists( dir_path ) )
        return;
    for ( auto &p : fs::directory_iterator( dir_path ) )
    {
        const fs::path &file_path = p.path();

        if ( file_path.extension() == ".mat" )
        {
            auto file = fopen( file_path.generic_string().c_str(), "rt" );
            char specFileName[80];
            char normalFileName[80];
            if ( file )
            {
                auto res = fscanf( file, "%79s\n", specFileName );
                if ( res == EOF )
                    fclose( file );
                fclose( file );

                g_specular_storage[file_path.stem().generic_string()] =
                    std::string( specFileName );
            }
        }
    }
    is_initialized = true;
}

RpMaterial *RpMaterialStreamRead( void *stream )
{
    auto material = RpMaterialStreamReadImpl( stream );
    if ( material == nullptr )
        return nullptr;
    auto mat_ext = GetBackendMaterialExt( material );
    if ( material->texture )
    {
        InitSpecStorage();
        auto string_s_name  = std::string( material->texture->name );
        auto spec_name_iter = g_specular_storage.find( string_s_name );
        if ( spec_name_iter != g_specular_storage.end() )
            mat_ext->mSpecTex =
                RwTextureRead( spec_name_iter->second.c_str(), nullptr );
    }
    return material;
}
BOOL emptystuff() { return false; }

BOOL WINAPI DllMain( HINSTANCE hModule, DWORD ul_reason_for_call,
                     LPVOID lpReserved )
{
    switch ( ul_reason_for_call )
    {
    case DLL_PROCESS_ATTACH:
    {
        rh::debug::DebugLogger::Init( "gtavc_logs.log",
                                      rh::debug::LogLevel::Info );
        rh::rw::engine::IPCSettings::mMode =
            rh::rw::engine::IPCRenderMode::CrossProcessClient;
        rh::rw::engine::IPCSettings::mProcessName = "gta_vc_render_driver.exe";
        RwPointerTable gtavc_ptr_table{};
        gtavc_ptr_table.mRwDevicePtr                  = 0x6DDE3C;
        gtavc_ptr_table.mRwRwDeviceGlobalsPtr         = 0x7DD708;
        gtavc_ptr_table.mIm3DOpen                     = 0x6431A0;
        gtavc_ptr_table.mIm3DClose                    = 0x6431A7;
        gtavc_ptr_table.m_fpCheckNativeTextureSupport = 0x602EAB;
        gtavc_ptr_table.mRasterRegisterPluginPtr      = 0x655370;
        gtavc_ptr_table.mCameraRegisterPluginPtr      = 0x64AAE0;
        gtavc_ptr_table.mMaterialRegisterPluginPtr    = 0x6558C0;
        gtavc_ptr_table.mAllocateResourceEntry        = 0x669330;

        gtavc_ptr_table.mFreeResourceEntry       = 0x669240; //
        gtavc_ptr_table.mAtomicGetHAnimHierarchy = 0x649950; //
        gtavc_ptr_table.mGeometryGetSkin         = 0x649960; //
        gtavc_ptr_table.mGetSkinToBoneMatrices   = 0x6499E0; //
        gtavc_ptr_table.mGetVertexBoneWeights    = 0x6499D0; //
        gtavc_ptr_table.mGetVertexBoneIndices    = 0;
        RwGameHooks::Patch( gtavc_ptr_table );
        //

        SetPointer( 0x6DF9AC,
                    reinterpret_cast<void *>( D3D8AtomicAllInOneNode ) );
        SetPointer(
            0x6DF8EC,
            reinterpret_cast<void *>( D3D8SkinAtomicAllInOneNode ) ); // skin
        // Hide default sky
        RedirectJump( 0x53F650, reinterpret_cast<void *>( true_hook ) );
        RedirectJump( 0x53F380, reinterpret_cast<void *>( true_hook ) );

        /// PBR tests
        RedirectCall( 0x66DD06,
                      reinterpret_cast<void *>( RpMaterialStreamRead ) );

        RedirectJump( 0x642B70,
                      reinterpret_cast<void *>( MyRwEngineGetVideoModeInfo ) );
        // Im3D
        SetPointer( 0x6DF754, reinterpret_cast<void *>( rxD3D8SubmitNode ) );

        RedirectJump( 0x65AE90, reinterpret_cast<void *>( RwIm3DTransform ) );
        RedirectJump( 0x65AF90, reinterpret_cast<void *>(
                                    RwIm3DRenderIndexedPrimitive ) );
        RedirectJump( 0x649C00, reinterpret_cast<void *>( RwIm3DRenderLine ) );
        RedirectJump( 0x65AF60, reinterpret_cast<void *>( RwIm3DEnd ) );

        // check dxt support
        RedirectJump( 0x61E310, reinterpret_cast<void *>( true_hook ) );
        // Transparent water is buggy with RTX
        RedirectCall( 0x4A65AE, reinterpret_cast<void *>( water_render ) );
        // Buggy cutscene shadows
        RedirectJump( 0x625D80, reinterpret_cast<void *>( emptystuff ) );
        // Lights
        RedirectJump( 0x567700, reinterpret_cast<void *>( AddPtLight ) );

        InitClient();
    }
    break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH: break;
    case DLL_PROCESS_DETACH:
    {
        ShutdownClient();
        break;
    }
    }
    return TRUE;
}