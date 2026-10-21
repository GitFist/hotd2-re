#include "d3d12_ray_scene.h"
#ifdef _WIN32
#include <windows.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <filesystem>
#include "hlsl_shaders.h"
#ifdef HOTD2_DXC_SDK
#include <dxcapi.h>
#endif

namespace port {
using Microsoft::WRL::ComPtr;
namespace {
bool CompilerAvailable(){
#ifdef HOTD2_DXC_SDK
    wchar_t executable[32768]{};const DWORD count=GetModuleFileNameW(nullptr,executable,32768);if(!count||count>=32768)return false;
    const auto directory=std::filesystem::path(executable).parent_path();
    HMODULE validator=LoadLibraryExW((directory/L"dxil.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!validator)return false;
    HMODULE runtime=LoadLibraryExW((directory/L"dxcompiler.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);bool ready=false;
    if(runtime){{auto create=reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(runtime,"DxcCreateInstance"));ComPtr<IDxcCompiler3> compiler;ready=create&&SUCCEEDED(create(CLSID_DxcCompiler,IID_PPV_ARGS(&compiler)));}FreeLibrary(runtime);}
    FreeLibrary(validator);return ready;
#else
    return false;
#endif
}
}
struct D3D12RayScene::Impl {
    ComPtr<ID3D12Device5> device;
    NativeD3D12RaytracingInfo info;
    std::vector<uint8_t> shader, vertexShader;
    std::vector<ComPtr<ID3D12Resource>> resources;
    D3D12_GPU_VIRTUAL_ADDRESS scene = 0;
    bool Compile(std::string*);
    ComPtr<ID3D12Resource> Buffer(UINT64 bytes, D3D12_HEAP_TYPE heapType,
        D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, const void* data = nullptr) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = heapType; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = (bytes + 255) & ~UINT64(255);
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags = flags;
        ComPtr<ID3D12Resource> r;
        if (!bytes || FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)))) return {};
        if (data) { void* mapped = nullptr; D3D12_RANGE range{}; if (FAILED(r->Map(0,&range,&mapped))) return {}; memcpy(mapped,data,static_cast<size_t>(bytes)); r->Unmap(0,nullptr); }
        resources.push_back(r); return r;
    }
};
D3D12RayScene::D3D12RayScene() : impl_(std::make_unique<Impl>()) {}
D3D12RayScene::~D3D12RayScene() = default;
void D3D12RayScene::Initialize(ID3D12Device* device) {
    auto& i = *impl_; i.info = {}; i.info.compilerAvailable=CompilerAvailable();i.device.Reset();
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{}; D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_5};
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&i.device))) || FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options,sizeof(options))) ||
        options.RaytracingTier < D3D12_RAYTRACING_TIER_1_1 || FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof(model))) || model.HighestShaderModel < D3D_SHADER_MODEL_6_5) {
        i.info.status = "DXR1.1 and Shader Model6.5 are required"; return;
    }
    i.info.supported = true;
#ifdef HOTD2_DXC_SDK
    i.info.status = i.info.compilerAvailable?"DXR1.1 available; shadows Off":"App-local official DXC compiler/validator runtime is unavailable";
#else
    i.info.status = "This build has no optional DXC SDK";
#endif
}
bool D3D12RayScene::Impl::Compile(std::string* error) {
#ifdef HOTD2_DXC_SDK
    wchar_t executable[32768]{}; const DWORD n = GetModuleFileNameW(nullptr,executable,32768);
    if (!n || n >= 32768) { if(error)*error="Cannot locate the application DXC runtime"; return false; }
    const auto dll = std::filesystem::path(executable).parent_path()/L"dxcompiler.dll";
    HMODULE module = LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) { if(error)*error="App-local official dxcompiler.dll/dxil.dll are unavailable"; return false; }
    bool okay = false;
    {
        auto create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module,"DxcCreateInstance"));
        ComPtr<IDxcCompiler3> compiler; ComPtr<IDxcResult> result; ComPtr<IDxcBlob> code; ComPtr<IDxcBlobUtf8> messages;
        if (create && SUCCEEDED(create(CLSID_DxcCompiler,IID_PPV_ARGS(&compiler)))) {
            const std::string source = R"(
RaytracingAccelerationStructure directionalScene : register(t3);
float TraceDirectionalVisibility(float3 position,float3 normal,float3 lightDirection) {
    RayDesc ray;
    ray.Origin=position+normal*0.15+lightDirection*0.05;
    ray.Direction=lightDirection;
    ray.TMin=0.05; ray.TMax=100000;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(directionalScene,RAY_FLAG_NONE,255,ray);
    while(query.Proceed()) {}
    return query.CommittedStatus()==COMMITTED_TRIANGLE_HIT?0.0:1.0;
}
#define DIRECT_LIGHT_VISIBILITY(P,N,L) TraceDirectionalVisibility(P,N,L)
)" + std::string(kModelShaderHlsl);
            const DxcBuffer buffer{source.data(),source.size(),DXC_CP_UTF8};
            okay = true;
            for (int stage = 0; stage < 2; ++stage) {
            result.Reset(); code.Reset(); messages.Reset();
            const wchar_t* args[] = {L"-E",stage ? L"psmain" : L"vsmain",L"-T",stage ? L"ps_6_5" : L"vs_6_5",L"-HV",L"2021",L"-O3"};
            if (SUCCEEDED(compiler->Compile(&buffer,args,static_cast<UINT32>(std::size(args)),nullptr,IID_PPV_ARGS(&result)))) {
                HRESULT status = E_FAIL; result->GetStatus(&status); result->GetOutput(DXC_OUT_ERRORS,IID_PPV_ARGS(&messages),nullptr);
                if (SUCCEEDED(status) && SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&code),nullptr))) {
                    const auto* start = static_cast<const uint8_t*>(code->GetBufferPointer()); auto& destination = stage ? shader : vertexShader; destination.assign(start,start+code->GetBufferSize());
                } else { okay = false; if (error) *error = messages && messages->GetStringLength() ? messages->GetStringPointer() : "DXR shader compilation failed"; }
            } else okay = false;
            if (!okay) break;
            }
        }
        if (!okay && error && error->empty()) *error = "DXC compiler initialization failed";
    }
    FreeLibrary(module); return okay;
#else
    if(error)*error="This build has no optional official DXC SDK"; return false;
#endif
}
bool D3D12RayScene::Configure(bool enabled,std::string* error) {
    auto& i=*impl_;
    if (!enabled) { i.info.enabled = false; i.info.status = "Directional ray shadows Off"; return true; }
    if (!i.info.supported) { if(error)*error=i.info.status; return false; }
    if (i.shader.empty() && !i.Compile(error)) return false;
    i.info.compilerAvailable = i.info.compiler = true; i.info.enabled = true; i.info.status = "DXR1.1 opaque directional shadows (experimental)"; return true;
}
void D3D12RayScene::BeginFrame() { impl_->resources.clear(); impl_->scene=0; impl_->info.geometries=impl_->info.triangles=0; }
const std::vector<uint8_t>& D3D12RayScene::PixelShader() const { return impl_->shader; }
const std::vector<uint8_t>& D3D12RayScene::VertexShader() const { return impl_->vertexShader; }
const NativeD3D12RaytracingInfo& D3D12RayScene::Info() const { return impl_->info; }
D3D12_GPU_VIRTUAL_ADDRESS D3D12RayScene::Build(ID3D12GraphicsCommandList* commands,const GpuDrawList& list,ID3D12Resource* const vertices[3],const UINT vertexBytes[3]) {
    auto& i=*impl_; if(!i.info.enabled || !i.device)return 0;
    ComPtr<ID3D12GraphicsCommandList4> rayCommands; if(FAILED(commands->QueryInterface(IID_PPV_ARGS(&rayCommands))))return 0;
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometry;
    std::vector<std::array<float,12>> transforms;
    const auto& casters = list.shadowCasters;
    const size_t end=casters.size(); transforms.reserve(end); geometry.reserve(end);
    // Only fully opaque depth-writing 3D triangles cast shadows. Blended smoke,
    // sprites and alpha-tested cutouts are excluded instead of solid card shadows.
    for(size_t d=0;d<end;++d) {
        const auto& draw=casters[d]; const int stream=static_cast<int>(draw.stream);
        if(draw.pipeline!=GpuDraw::Pipeline::Model || stream<0 || stream>1 || !vertices[stream] || !draw.state.zwrite || draw.state.blendEnable || draw.constants>=list.model.size() || draw.count<3 || draw.count%3 ||
            uint64_t(draw.first)+draw.count>vertexBytes[stream]/sizeof(game::ModelVertex) || list.model[draw.constants].flags[2]>=0)continue;
        const auto& c=list.model[draw.constants]; std::array<float,12> transform{};
        for(int row=0;row<3;++row)for(int column=0;column<4;++column)transform[row*4+column]=c.world[column*4+row];
        transforms.push_back(transform);
        D3D12_RAYTRACING_GEOMETRY_DESC g{};g.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;g.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        g.Triangles.VertexBuffer.StartAddress=vertices[stream]->GetGPUVirtualAddress()+uint64_t(draw.first)*sizeof(game::ModelVertex);
        g.Triangles.VertexBuffer.StrideInBytes=sizeof(game::ModelVertex);g.Triangles.VertexCount=draw.count;g.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.push_back(g);i.info.triangles+=draw.count/3;
    }
    if(geometry.empty())return 0;
    auto transform=i.Buffer(transforms.size()*sizeof(transforms[0]),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE,transforms.data());if(!transform)return 0;
    for(size_t g=0;g<geometry.size();++g)geometry[g].Triangles.Transform3x4=transform->GetGPUVirtualAddress()+g*sizeof(transforms[0]);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS input{};input.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;input.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;
    input.NumDescs=static_cast<UINT>(geometry.size());input.pGeometryDescs=geometry.data();input.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size{};i.device->GetRaytracingAccelerationStructurePrebuildInfo(&input,&size);
    auto blas=i.Buffer(size.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto scratch=i.Buffer(size.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);if(!blas||!scratch)return 0;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs=input;build.DestAccelerationStructureData=blas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();
    rayCommands->BuildRaytracingAccelerationStructure(&build,0,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=blas.Get();commands->ResourceBarrier(1,&barrier);
    D3D12_RAYTRACING_INSTANCE_DESC instance{};instance.Transform[0][0]=instance.Transform[1][1]=instance.Transform[2][2]=1;
    instance.InstanceMask=255;instance.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;instance.AccelerationStructure=blas->GetGPUVirtualAddress();
    auto instances=i.Buffer(sizeof(instance),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE,&instance);if(!instances)return 0;
    input={};input.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;input.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;input.NumDescs=1;
    input.InstanceDescs=instances->GetGPUVirtualAddress();input.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    i.device->GetRaytracingAccelerationStructurePrebuildInfo(&input,&size);
    auto tlas=i.Buffer(size.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    scratch=i.Buffer(size.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);if(!tlas||!scratch)return 0;
    build={};build.Inputs=input;build.DestAccelerationStructureData=tlas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();
    rayCommands->BuildRaytracingAccelerationStructure(&build,0,nullptr);barrier.UAV.pResource=tlas.Get();commands->ResourceBarrier(1,&barrier);
    i.scene=tlas->GetGPUVirtualAddress();i.info.geometries=static_cast<uint32_t>(geometry.size());
    return i.scene;
}
} // namespace port
#endif
