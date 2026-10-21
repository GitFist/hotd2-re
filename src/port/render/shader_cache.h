#pragma once
#ifdef _WIN32
#include <windows.h>
#include <d3dcompiler.h>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace port {
struct ShaderCacheCounters {uint64_t hits=0,compiles=0,invalid=0;};
namespace shader_cache_detail {
inline std::atomic<uint64_t> hits{0},compiles{0},invalid{0},writes{0};
inline uint64_t Hash(uint64_t hash,const void* data,size_t length) {
    const auto* bytes=static_cast<const uint8_t*>(data);for(size_t i=0;i<length;++i){hash^=bytes[i];hash*=1099511628211ull;}return hash;
}
inline uint64_t Text(uint64_t hash,const char* text){const char zero=0;if(text)hash=Hash(hash,text,std::strlen(text));return Hash(hash,&zero,1);}
inline std::wstring Environment(const wchar_t* name) {
    const DWORD needed=GetEnvironmentVariableW(name,nullptr,0);if(!needed||needed>32768)return {};
    std::wstring value(needed,L'\0');const DWORD actual=GetEnvironmentVariableW(name,value.data(),needed);if(!actual||actual>=needed)return {};value.resize(actual);return value;
}
inline std::filesystem::path Directory() {
    auto overridePath=Environment(L"HOTD2_SHADER_CACHE_DIR");if(!overridePath.empty())return overridePath;
    auto local=Environment(L"LOCALAPPDATA");if(local.empty())return {};return std::filesystem::path(local)/L"Hotd2Re"/L"ShaderCache"/L"dxbc-v1";
}
struct Header {uint32_t magic=0x32484453,version=1,size=0,reserved=0;uint64_t key=0,checksum=0;};
static_assert(sizeof(Header)==32);
inline uint64_t CompilerKey(uint64_t hash) {
    HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&D3DCompile),&module);
    wchar_t path[32768];const DWORD size=module?GetModuleFileNameW(module,path,32768):0;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if(size&&size<32768&&GetFileAttributesExW(path,GetFileExInfoStandard,&info)) {
        hash=Hash(hash,&info.ftLastWriteTime,sizeof(info.ftLastWriteTime));hash=Hash(hash,&info.nFileSizeLow,sizeof(info.nFileSizeLow));hash=Hash(hash,&info.nFileSizeHigh,sizeof(info.nFileSizeHigh));
    }
    return hash;
}
inline std::filesystem::path Path(const std::filesystem::path& directory,uint64_t key) {
    wchar_t name[40];swprintf_s(name,L"%016llx.dxbc",static_cast<unsigned long long>(key));return directory/name;
}
} // namespace shader_cache_detail

inline ShaderCacheCounters GetShaderCacheCounters() {
    return {shader_cache_detail::hits.load(),shader_cache_detail::compiles.load(),shader_cache_detail::invalid.load()};
}

// Exact DXBC compilation cache. The include callback path bypasses the cache
// because external include contents are not part of the source key. Driver PSO
// creation remains fresh; compiled DXBC is independent of GPU vendor/driver.
inline HRESULT CompileShaderCached(const void* source,SIZE_T length,const char* sourceName,const D3D_SHADER_MACRO* macros,
                                  ID3DInclude* includes,const char* entry,const char* profile,UINT flags1,UINT flags2,
                                  ID3DBlob** code,ID3DBlob** errors) {
    using namespace shader_cache_detail;
    if(!code||!source||!length||includes)return D3DCompile(source,length,sourceName,macros,includes,entry,profile,flags1,flags2,code,errors);
    *code=nullptr;if(errors)*errors=nullptr;
    uint64_t key=CompilerKey(1469598103934665603ull);key=Hash(key,source,length);key=Text(key,sourceName);key=Text(key,entry);key=Text(key,profile);
    key=Hash(key,&flags1,sizeof(flags1));key=Hash(key,&flags2,sizeof(flags2));
    if(macros)for(const auto* macro=macros;macro->Name;++macro){key=Text(key,macro->Name);key=Text(key,macro->Definition);}
    const auto directory=Directory();const auto path=directory.empty()?std::filesystem::path{}:Path(directory,key);
    try {
        if(!path.empty()){std::ifstream input(path,std::ios::binary|std::ios::ate);
            if(input){const auto size=input.tellg();input.seekg(0);Header header;input.read(reinterpret_cast<char*>(&header),sizeof(header));
                if(input&&header.magic==Header{}.magic&&header.version==1&&header.key==key&&header.size>=4&&header.size<=4*1024*1024&&
                   size==static_cast<std::streamoff>(sizeof(header)+header.size)){
                    std::vector<uint8_t> bytes(header.size);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
                    if(input&&!std::memcmp(bytes.data(),"DXBC",4)&&Hash(1469598103934665603ull,bytes.data(),bytes.size())==header.checksum&&
                       SUCCEEDED(D3DCreateBlob(bytes.size(),code))){std::memcpy((*code)->GetBufferPointer(),bytes.data(),bytes.size());++hits;return S_OK;}}
                ++invalid;
            }}
    }catch(const std::exception&){} // Cache failure never prevents actual compilation.
    ++compiles;const HRESULT result=D3DCompile(source,length,sourceName,macros,includes,entry,profile,flags1,flags2,code,errors);
    if(FAILED(result)||!*code||path.empty()||(*code)->GetBufferSize()>4*1024*1024)return result;
    try {
        std::error_code ec;std::filesystem::create_directories(directory,ec);if(ec)return result;
        const auto temporary=std::filesystem::path(path.wstring()+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++writes));
        Header header;header.key=key;header.size=static_cast<uint32_t>((*code)->GetBufferSize());header.checksum=Hash(1469598103934665603ull,(*code)->GetBufferPointer(),header.size);
        {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(&header),sizeof(header));output.write(static_cast<const char*>((*code)->GetBufferPointer()),header.size);if(!output)return result;}
        MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
    }catch(const std::exception&){}
    return result;
}
} // namespace port
#endif
