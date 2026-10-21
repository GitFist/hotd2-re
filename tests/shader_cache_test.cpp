#include "render/shader_cache.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){wchar_t temporary[32768];Require(GetTempPathW(32768,temporary)>0,"find task temporary directory");
    const auto directory=std::filesystem::path(temporary)/(L"hotd2-shader-cache-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::error_code ec;std::filesystem::create_directories(directory,ec);Require(!ec,"create isolated cache directory");SetEnvironmentVariableW(L"HOTD2_SHADER_CACHE_DIR",directory.c_str());
    const char* source="float4 psmain():SV_Target{return float4(.2,.4,.6,1);}";
    auto compile=[&](const char* text,const D3D_SHADER_MACRO* macros=nullptr,UINT flags=0){ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
        const HRESULT result=port::CompileShaderCached(text,std::strlen(text),"cache-test",macros,nullptr,"psmain","ps_5_0",flags,0,&code,&errors);
        if(FAILED(result))std::fprintf(stderr,"%s\n",errors?static_cast<const char*>(errors->GetBufferPointer()):"compile failed");
        if(errors)errors->Release();Require(SUCCEEDED(result)&&code,"compile actual DXBC");std::string bytes(static_cast<const char*>(code->GetBufferPointer()),code->GetBufferSize());code->Release();return bytes;};
    const auto first=compile(source);const auto initial=port::GetShaderCacheCounters();Require(initial.compiles==1&&initial.hits==0,"first shader did not compile");
    Require(compile(source)==first,"cached DXBC changed bytecode");const auto hit=port::GetShaderCacheCounters();Require(hit.hits==1&&hit.compiles==1,"exact source did not use cache");
    const auto changed=compile("float4 psmain():SV_Target{return float4(.6,.4,.2,1);}");Require(changed!=first,"changed shader reused old source bytecode");
    const D3D_SHADER_MACRO macro[]={{"CACHE_VARIANT","1"},{nullptr,nullptr}};compile(source,macro);compile(source,nullptr,D3DCOMPILE_SKIP_OPTIMIZATION);
    Require(port::GetShaderCacheCounters().compiles==4,"source/macros/compiler flags are not independent cache keys");
    for(const auto& file:std::filesystem::directory_iterator(directory))if(file.path().extension()==L".dxbc"){
        std::fstream corrupt(file.path(),std::ios::in|std::ios::out|std::ios::binary);corrupt.seekp(24);const uint64_t badChecksum=0;corrupt.write(reinterpret_cast<const char*>(&badChecksum),sizeof(badChecksum));}
    Require(compile(source)==first,"corrupt cache did not recover through compilation");const auto recovered=port::GetShaderCacheCounters();Require(recovered.invalid==1&&recovered.compiles==5,"corrupt bytecode was accepted");
    // Cache IO errors are nonfatal: this override names a regular file, so no
    // cache directory can be created beneath it. Real compilation still works.
    const auto blocked=directory/L"not-a-directory";{std::ofstream file(blocked);file<<"x";}
    SetEnvironmentVariableW(L"HOTD2_SHADER_CACHE_DIR",blocked.c_str());Require(compile(source)==first,"cache IO failure prevented shader compilation");
    SetEnvironmentVariableW(L"HOTD2_SHADER_CACHE_DIR",nullptr);
    // Every file was created in this process's dedicated directory. No recursive
    // removal or traversal through paths outside that exact directory is used.
    for(const auto& file:std::filesystem::directory_iterator(directory))std::filesystem::remove(file.path(),ec);std::filesystem::remove(directory,ec);
    std::puts("PASS actual DXBC cache hits, exact bytecode, source/macro/flag invalidation, corruption recovery and nonfatal IO errors");
}
