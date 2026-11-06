#include "naomi_visuals.h"
#include "game/prs.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace port {
namespace {
constexpr size_t kMaxBank = 512u << 20;
uint32_t Crc32(const uint8_t* p,size_t n) {
    uint32_t c=~0u;
    for(size_t i=0;i<n;++i){c^=p[i];for(int b=0;b<8;++b)c=(c>>1)^(0xEDB88320u&(0u-(c&1u)));}
    return ~c;
}
struct Reader {
    const uint8_t* p; size_t left;
    void Need(size_t n) const { if(n>left)throw std::runtime_error("Truncated Naomi visual bank"); }
    uint32_t U32(){Need(4);uint32_t v=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);p+=4;left-=4;return v;}
    std::vector<uint8_t> Bytes(size_t n){Need(n);std::vector<uint8_t> v(p,p+n);p+=n;left-=n;return v;}
    std::string Name(){const auto n=U32();if(!n||n>80)throw std::runtime_error("Invalid Naomi resource name");Need(n);std::string s(reinterpret_cast<const char*>(p),n);p+=n;left-=n;
        if(s.size()<5||s.substr(s.size()-4)!=".bin"||s.find("..")!=std::string::npos||!std::all_of(s.begin(),s.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.';}))throw std::runtime_error("Invalid Naomi resource name");return s;}
};
bool Dimension(int v){return v>0&&v<=2048&&(v&(v-1))==0;}
size_t Required(const game::TextureDesc& d){const size_t n=size_t(d.width)*size_t(d.height);switch((d.format>>8)&255){case 1:case 9:case 13:return n*2;case 3:return 2048+n/4;case 5:return(n+1)/2;case 7:return n;default:throw std::runtime_error("Unsupported Naomi texture layout");}}
std::vector<uint8_t> Unpack(const NaomiVisuals::Resource& r){
    if(!r.prs)return r.bytes;
    std::vector<uint8_t> out(r.unpackedSize);
    if(r.bytes.size()<4||game::DecompressPrsSafe(r.bytes.data()+4,r.bytes.size()-4,out.data(),out.size())!=static_cast<int>(out.size()))throw std::runtime_error("Invalid Naomi PRS resource");
    return out;
}
uint32_t Word(const std::vector<uint8_t>& b,size_t p){if(p>b.size()||b.size()-p<4)throw std::runtime_error("Naomi model word outside resource");uint32_t v;std::memcpy(&v,b.data()+p,4);return v;}
std::vector<game::PolEntry> Headers(const std::vector<uint8_t>& b){
    uint32_t previous=0;size_t count=0;bool ended=false;
    for(;count<=4096&&count*8+8<=b.size();++count){const auto start=Word(b,count*8)&0x7FFFFFFFu;if(start==b.size()){ended=true;break;}if(start<previous||start>b.size())break;previous=start;}
    if(!ended||!count||count>4096)throw std::runtime_error("Invalid Naomi model header table");
    const size_t table=(count+1)*8;auto result=game::ReadPolHeader(b.data(),b.size());
    for(const auto& e:result)if(e.start<table||e.end<e.start||e.end>b.size())throw std::runtime_error("Invalid Naomi model block bounds");return result;
}
void Finite(const std::vector<uint8_t>& b,size_t at,size_t count){
    if(at>b.size()||count>(b.size()-at)/4)throw std::runtime_error("Naomi model float outside resource");
    for(size_t k=0;k<count;++k){float v;std::memcpy(&v,b.data()+at+k*4,4);if(!std::isfinite(v))throw std::runtime_error("Non-finite Naomi model data");}
}
// Bound vertex counts BEFORE the shared original-data parser resizes vectors.
// Native banks are external files, so their own checksum is not a bounds check.
void ValidateModel(const std::vector<uint8_t>& b,const game::PolEntry& e){
    const size_t root=e.root,end=e.end;if(root<e.start||root>end||end-root<24||Word(b,root+4)&0x10)throw std::runtime_error("Unsupported Naomi model representation");Finite(b,root+8,4);
    size_t pos=root+24,stripEnd=0,vertices=0,meshes=0,strips=0;bool material=false;
    for(;;){
        if(pos>end||end-pos<4)throw std::runtime_error("Naomi model chunk outside block");const auto ctrl=Word(b,pos);if(!ctrl){if(stripEnd&&pos!=stripEnd)throw std::runtime_error("Naomi model strip size mismatch");return;}
        if(ctrl&0x80000000u){
            if(end-pos<80||(stripEnd&&pos!=stripEnd)||++meshes>4096)throw std::runtime_error("Invalid Naomi model material");const size_t bytes=Word(b,pos+76)&~3u;
            if(bytes>end-pos-80)throw std::runtime_error("Naomi model material exceeds block");stripEnd=pos+80+bytes;material=true;Finite(b,pos+16,4);Finite(b,pos+40,5);Finite(b,pos+64,3);pos+=80;continue;
        }
        if(!material||end-pos<8||++strips>65536)throw std::runtime_error("Invalid Naomi model strip");uint64_t count=Word(b,pos+4);if(ctrl&8)count*=3;
        const size_t unit=(ctrl&0x20)?8:32;if(count>(end-pos-8)/unit||count>1000000-vertices)throw std::runtime_error("Naomi model vertex count exceeds block");vertices+=static_cast<size_t>(count);pos+=8;
        for(size_t k=0;k<count;++k){
            if(ctrl&0x20){
                if(end-pos<8)throw std::runtime_error("Invalid Naomi vertex reference");
                if(Word(b,pos)&1){if(end-pos<32)throw std::runtime_error("Invalid Naomi inline vertex");Finite(b,pos,8);pos+=32;}
                else{const int32_t off=static_cast<int32_t>(Word(b,pos+4));const int64_t target=static_cast<int64_t>(pos)+8+off;if(target<static_cast<int64_t>(root)||target+32>static_cast<int64_t>(end))throw std::runtime_error("Naomi vertex reference outside model");Finite(b,static_cast<size_t>(target),8);pos+=8;}
            }else{if(end-pos<32)throw std::runtime_error("Naomi vertex outside model");Finite(b,pos,8);pos+=32;}
        }
    }
}
}

bool NaomiVisuals::SupportedTexture(const game::TextureDesc& d){const auto l=(d.format>>8)&255;return Dimension(d.width)&&Dimension(d.height)&&(d.format&7)<=2&&(l==1||l==3||l==5||l==7||l==9||l==13);}
bool NaomiVisuals::Inspect(const std::filesystem::path& path,std::string* error){
    try{std::error_code ec;const auto size=std::filesystem::file_size(path,ec);if(ec||size<32||size>kMaxBank)throw std::runtime_error("Naomi visual assets are not installed or have invalid size");
        uint8_t bytes[32];std::ifstream f(path,std::ios::binary);if(!f.read(reinterpret_cast<char*>(bytes),32)||std::memcmp(bytes,"H2NVIS01",8))throw std::runtime_error("Unrecognized Naomi visual bank");Reader h{bytes+8,24};
        const auto version=h.U32();if((version!=1&&version!=2)||h.U32()!=size-32)throw std::runtime_error("Unsupported Naomi visual bank header");h.U32();const auto resources=h.U32(),aliases=h.U32(),models=h.U32();if(!resources||resources>1024||aliases>100000||models>100000)throw std::runtime_error("Invalid Naomi visual bank counts");if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
const NaomiVisuals::Resource* NaomiVisuals::Find(uint32_t kind,const std::string& bank) const {auto i=resources_.find({kind,bank});return i==resources_.end()?nullptr:&i->second;}
const NaomiVisuals::TextureRef* NaomiVisuals::TextureMapping(const std::string& bank,uint32_t index) const {auto i=textureMap_.find({bank,index});return i==textureMap_.end()?nullptr:&i->second;}
const NaomiVisuals::ModelRef* NaomiVisuals::ModelMapping(const std::string& bank,uint32_t index) const {auto i=modelMap_.find({bank,index});return i==modelMap_.end()?nullptr:&i->second;}
const game::TextureDesc* NaomiVisuals::Descriptor(const std::string& bank,uint32_t index) const {const auto* r=Find(1,bank);return r&&index<r->textures.size()?&r->textures[index]:nullptr;}
const NaomiVisuals::TextureRef* NaomiVisuals::BloodTexture(uint32_t id,bool red) const {const auto& table=red?red_:green_;auto i=table.find(id);return i==table.end()?nullptr:&i->second;}
const std::vector<uint8_t>* NaomiVisuals::Data(uint32_t kind,const std::string& bank){const ResourceKey k{kind,bank};if(auto i=decoded_.find(k);i!=decoded_.end())return&i->second;const auto* r=Find(kind,bank);if(!r)return nullptr;return&decoded_.emplace(k,Unpack(*r)).first->second;}
void NaomiVisuals::ClearDecoded(){parsed_.clear();decoded_.clear();}
bool NaomiVisuals::CanDecodeTexture(const std::string& bank,uint32_t index) const {
    const auto* d=Descriptor(bank,index);if(!d||!SupportedTexture(*d))return false;const auto layout=(d->format>>8)&255;
    if(layout!=5&&layout!=7)return true;const auto p=paletteMap_.find({bank,index});return p!=paletteMap_.end()&&palettes_.count(p->second)!=0;
}
bool NaomiVisuals::DecodeRgba(const std::string& bank,uint32_t index,std::vector<uint8_t>& out){
    if(!CanDecodeTexture(bank,index))return false;const auto& d=*Descriptor(bank,index);const auto* bytes=Data(1,bank);if(!bytes)return false;
    const auto layout=(d.format>>8)&255;std::vector<uint16_t> texels;auto format=game::PixelFormatOf(d);
    if(layout==5||layout==7){
        const auto& palette=palettes_.at(paletteMap_.at({bank,index}));format=palette.format;texels.resize(size_t(d.width)*d.height);const uint32_t square=std::min(d.width,d.height);
        for(uint32_t y=0;y<uint32_t(d.height);++y)for(uint32_t x=0;x<uint32_t(d.width);++x){
            const uint32_t block=d.width>=d.height?x/square:y/square;const uint32_t t=block*square*square+game::Twiddle(x%square,y%square);const size_t at=size_t(d.offset)+(layout==5?t/2:t);
            if(at>=bytes->size())return false;const auto color=layout==5?((*bytes)[at]>>(4*(t&1)))&15:(*bytes)[at];if(color>=palette.colors.size())return false;texels[size_t(y)*d.width+x]=palette.colors[color];
        }
    }else if(!game::DecodeTexels(bytes->data(),bytes->size(),d,nullptr,texels))return false;
    out.resize(texels.size()*4);return game::TexelsToRgba(texels.data(),texels.size(),format,out.data());
}
const game::Model* NaomiVisuals::NativeModel(const std::string& bank,uint32_t index){
    const Key key{bank,index};auto found=parsed_.find(key);
    if(found==parsed_.end()){
        const auto* bytes=Data(0,bank);if(!bytes)return nullptr;const auto headers=Headers(*bytes);
        if(index>=headers.size()||headers[index].flag)return nullptr;game::Model m;
        ValidateModel(*bytes,headers[index]);
        if(!game::ParseModel(bytes->data(),bytes->size(),headers[index],m)||m.flags&0x10)return nullptr;
        for(const auto& mesh:m.meshes)if(mesh.material.Textured()&&!CanDecodeTexture(bank,mesh.material.TextureRef()))return nullptr;
        found=parsed_.emplace(key,std::move(m)).first;
    }
    return &found->second;
}
bool NaomiVisuals::Model(const std::string& bank,uint32_t index,game::Model& out,std::string* nativeBank){
    const auto* ref=ModelMapping(bank,index);if(!ref)return false;const auto* source=NativeModel(ref->bank,ref->index);if(!source)return false;out=*source;if(nativeBank)*nativeBank=ref->bank;return true;
}
bool NaomiVisuals::Mesh(const std::string& bank,uint32_t model,uint32_t mesh,game::ModelMesh& out,std::string* nativeBank,uint32_t* evidence){
    if(evidence)*evidence=0;const game::Model* source=nullptr;std::string name;uint32_t index=mesh,proof=0;
    if(const auto found=meshMap_.find({bank,model,mesh});found!=meshMap_.end()){proof=found->second.evidence;if((proof==12||proof==16)&&!evidence)return false;name=found->second.bank;index=found->second.mesh;source=NativeModel(name,found->second.model);}
    else {const auto* ref=ModelMapping(bank,model);if(!ref||(ref->evidence!=1&&ref->evidence!=3))return false;proof=ref->evidence;name=ref->bank;source=NativeModel(name,ref->index);}
    if(!source||index>=source->meshes.size())return false;out=source->meshes[index];if(nativeBank)*nativeBank=name;if(evidence)*evidence=proof;return true;
}

bool NaomiVisuals::Load(const std::filesystem::path& path,std::string* error){
    try{
        std::error_code ec;const auto size=std::filesystem::file_size(path,ec);if(ec||size<32||size>kMaxBank)throw std::runtime_error("Naomi visual bank missing or invalid size");
        std::ifstream f(path,std::ios::binary);std::vector<uint8_t> bytes(static_cast<size_t>(size));if(!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Cannot read Naomi visual bank");
        if(std::memcmp(bytes.data(),"H2NVIS01",8))throw std::runtime_error("Unrecognized Naomi visual bank");Reader h{bytes.data()+8,bytes.size()-8};
        const auto version=h.U32();if(version!=1&&version!=2)throw std::runtime_error("Unsupported Naomi visual bank version");const auto payload=h.U32(),crc=h.U32(),resourceCount=h.U32(),aliasCount=h.U32(),modelCount=h.U32();
        if(payload!=bytes.size()-32||Crc32(bytes.data()+32,payload)!=crc)throw std::runtime_error("Naomi visual bank checksum mismatch");
        if(!resourceCount||resourceCount>1024||aliasCount>100000||modelCount>100000)throw std::runtime_error("Naomi visual bank count limit");
        NaomiVisuals next;Reader r{bytes.data()+32,payload};
        for(uint32_t i=0;i<resourceCount;++i){const auto kind=r.U32();const auto name=r.Name();const auto count=r.U32(),flags=r.U32(),unpacked=r.U32(),stored=r.U32();
            if(kind>1||(kind==0&&count)||count>4096||flags>1||!stored||stored>(64u<<20)||!unpacked||unpacked>(64u<<20)||(!flags&&unpacked!=stored))throw std::runtime_error("Invalid Naomi resource record");
            Resource v;v.kind=kind;v.prs=flags!=0;v.unpackedSize=unpacked;v.textures.reserve(count);
            for(uint32_t j=0;j<count;++j){const auto wh=r.U32();game::TextureDesc d;d.width=static_cast<int16_t>(wh&65535);d.height=static_cast<int16_t>(wh>>16);d.format=r.U32();d.offset=r.U32();d.id=r.U32();
                if(!Dimension(d.width)||!Dimension(d.height)||(d.format&7)>2||d.offset>unpacked||Required(d)>unpacked-d.offset)throw std::runtime_error("Naomi texture descriptor outside resource");v.textures.push_back(d);}
            v.bytes=r.Bytes(stored);const auto decoded=Unpack(v);
            if(decoded.size()!=unpacked)throw std::runtime_error("Naomi resource size mismatch");
            if(kind==0)Headers(decoded);
            if(!next.resources_.emplace(ResourceKey{kind,name},std::move(v)).second)throw std::runtime_error("Duplicate Naomi resource");
        }
        if(version>=2){
            const auto count=r.U32();if(count>64)throw std::runtime_error("Too many Naomi palette records");
            for(uint32_t i=0;i<count;++i){const auto id=r.U32(),format=r.U32(),colors=r.U32();if(id>63||format>2||!colors||colors>256)throw std::runtime_error("Invalid Naomi palette record");Palette p;p.format=static_cast<game::PixelFormat>(format);const auto raw=r.Bytes(colors*2);p.colors.resize(colors);for(uint32_t j=0;j<colors;++j)p.colors[j]=uint16_t(raw[j*2])|(uint16_t(raw[j*2+1])<<8);if(!next.palettes_.emplace(id,std::move(p)).second)throw std::runtime_error("Duplicate Naomi palette");}
            const auto bindings=r.U32();if(bindings>100000)throw std::runtime_error("Too many Naomi palette bindings");
            for(uint32_t i=0;i<bindings;++i){const auto bank=r.Name();const auto index=r.U32(),palette=r.U32();const auto* d=next.Descriptor(bank,index);const auto found=next.palettes_.find(palette);if(!d||found==next.palettes_.end()||(((d->format>>8)&255)!=5&&((d->format>>8)&255)!=7)||(((d->format>>8)&255)==5&&found->second.colors.size()>16)||!next.paletteMap_.emplace(Key{bank,index},palette).second)throw std::runtime_error("Invalid Naomi palette binding");}
            const auto meshes=r.U32();if(meshes>100000)throw std::runtime_error("Too many Naomi mesh bindings");
            for(uint32_t i=0;i<meshes;++i){const auto pc=r.Name();const auto part=r.U32(),mesh=r.U32();MeshRef ref{r.Name(),r.U32(),r.U32(),r.U32()};if(part>4095||mesh>4095||ref.model>4095||ref.mesh>4095||!next.Find(0,ref.bank)||!next.meshMap_.emplace(std::make_tuple(pc,part,mesh),std::move(ref)).second)throw std::runtime_error("Invalid Naomi mesh binding");}
        }
        for(uint32_t i=0;i<aliasCount;++i){const auto pc=r.Name();const auto pi=r.U32();TextureRef ref{r.Name(),r.U32()};
            if(pi>4095||!next.CanDecodeTexture(ref.bank,ref.index)||!next.textureMap_.emplace(Key{pc,pi},std::move(ref)).second)throw std::runtime_error("Invalid Naomi texture correspondence");}
        for(uint32_t i=0;i<modelCount;++i){const auto pc=r.Name();const auto pi=r.U32();ModelRef ref{r.Name(),r.U32(),r.U32()};const auto* resource=next.Find(0,ref.bank);
            if(pi>4095||ref.index>4095||!resource||!next.modelMap_.emplace(Key{pc,pi},std::move(ref)).second)throw std::runtime_error("Invalid Naomi model correspondence");}
        if(r.left)throw std::runtime_error("Unexpected trailing Naomi bank data");
        for(const auto& [key,ref]:next.modelMap_){game::Model test;if(!next.Model(key.first,key.second,test,nullptr))throw std::runtime_error("Mapped Naomi model cannot be decoded safely");}
        for(const auto& [key,ref]:next.meshMap_){game::ModelMesh test;uint32_t evidence=0;if(!next.Mesh(std::get<0>(key),std::get<1>(key),std::get<2>(key),test,nullptr,&evidence))throw std::runtime_error("Mapped Naomi mesh cannot be decoded safely");}
        for(const auto& [key,palette]:next.paletteMap_){std::vector<uint8_t> test;if(!next.DecodeRgba(key.first,key.second,test))throw std::runtime_error("Naomi palette does not cover texture indices");}
        next.ClearDecoded();
        for(const auto& [name,red]:{std::pair<const char*,bool>{"scr_blood_red.bin",true},{"scr_blood_green.bin",false}}){const auto* resource=next.Find(1,name);if(!resource)throw std::runtime_error("Naomi blood bank is missing");auto& table=red?next.red_:next.green_;for(uint32_t i=0;i<resource->textures.size();++i)if(SupportedTexture(resource->textures[i]))table.emplace(resource->textures[i].id,TextureRef{name,i});}
        next.available_=true;*this=std::move(next);if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
} // namespace port
