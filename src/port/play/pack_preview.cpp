#include "play/pack_preview.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>

#include "render/png_read.h"

namespace play {
namespace {
namespace fs = std::filesystem;
constexpr size_t kMaxFileBytes = 8u << 20;
constexpr size_t kMaxRgbaBytes = 16u << 20;
constexpr size_t kMaxListing = 2048;
constexpr size_t kMaxCandidateFiles = 32;
constexpr int kWidth = 256, kHeight = 192;

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string Trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : s.substr(first, s.find_last_not_of(" \t\r\n")-first+1);
}
std::string Label(std::string s) {
    s = Trim(std::move(s));
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; }), s.end());
    if (s.size() > 120) s.resize(120);
    return s;
}
std::string Identity(const fs::path& path) {
    auto s = path.lexically_normal().generic_string();
#ifdef _WIN32
    s = Lower(std::move(s));
#endif
    return s;
}
bool Inside(const fs::path& root, const fs::path& candidate, fs::path* resolved = nullptr) {
    std::error_code ec;
    const auto canonical = fs::weakly_canonical(candidate, ec);
    if (ec) return false;
    auto p = canonical.begin();
    for (const auto& component : root) {
        if (p == canonical.end() || Identity(component) != Identity(*p++)) return false;
    }
    if (p == canonical.end()) return false; // a preview is a file beneath the pack
    if (resolved) *resolved = canonical;
    return true;
}
bool ExplicitPath(const fs::path& root, std::string value, fs::path* out) {
    // Reject alternate separators too, so a manifest has the same meaning across
    // platforms. Canonical containment separately rejects junction/symlink escapes.
    std::replace(value.begin(), value.end(), '\\', '/');
    if (value.empty() || value.size() > 512 || value.find(':') != std::string::npos || value.front() == '/') return false;
    const fs::path relative(value);
    for (const auto& c : relative) if (c == ".." || c == ".") return false;
    if (relative.has_root_path() || Lower(relative.extension().string()) != ".png") return false;
    return Inside(root, root / relative, out);
}
bool Decode(const fs::path& root, const fs::path& path, port::PngImage* image, std::string* error) {
    fs::path resolved;
    std::error_code ec;
    if (!Inside(root, path, &resolved) || !fs::is_regular_file(resolved, ec) || ec) { *error = "Preview path is outside the pack or unreadable"; return false; }
    const auto size = fs::file_size(resolved, ec);
    if (ec || size < 33 || size > kMaxFileBytes) { *error = "Preview image exceeds the 8 MiB file limit or is unreadable"; return false; }
    std::ifstream stream(resolved, std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.peek() != std::char_traits<char>::eof()) { *error = "Preview changed while reading"; return false; }
    constexpr uint8_t signature[] = {137,80,78,71,13,10,26,10};
    if (std::memcmp(bytes.data(), signature, 8) || bytes[8] || bytes[9] || bytes[10] || bytes[11] != 13 ||
        std::memcmp(bytes.data()+12, "IHDR", 4) || bytes[26] || bytes[27] || bytes[28]) { *error = "Preview requires a non-interlaced PNG"; return false; }
    auto be32 = [](const uint8_t* p) { return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3]; };
    const uint32_t w = be32(bytes.data()+16), h = be32(bytes.data()+20);
    if (!w || !h || w > 4096 || h > 4096 || uint64_t(w)*h*4 > kMaxRgbaBytes) { *error = "Preview exceeds the 4096-pixel / 16 MiB raster limit"; return false; }
    const auto depth=bytes[24],color=bytes[25];
    const bool supported=(color==0&&(depth==1||depth==2||depth==4||depth==8||depth==16)) ||
        ((color==2||color==4||color==6)&&(depth==8||depth==16)) || (color==3&&(depth==1||depth==2||depth==4||depth==8));
    if (!supported) { *error="Preview PNG color format is unsupported";return false; }
    return port::ReadPngLimited(bytes.data(), bytes.size(), kMaxRgbaBytes, *image, error);
}
std::vector<fs::path> Children(const fs::path& root, const fs::path& directory, size_t& remaining, bool* limited) {
    std::vector<fs::path> result;
    if (!Inside(root, directory)) return result;
    std::error_code ec;
    for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end; it != end && !ec; it.increment(ec)) {
        if (!remaining) { *limited = true; break; }
        --remaining;
        if (Inside(root, it->path())) result.push_back(it->path());
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return Identity(a) < Identity(b); });
    return result;
}
std::vector<fs::path> Candidates(const fs::path& root, bool* limited) {
    std::vector<fs::path> files;
    size_t remaining = kMaxListing;
    std::error_code ec;
    const auto banks = Children(root, root/"tex", remaining, limited);
    for (const auto& bank : banks) {
        if (!fs::is_directory(bank, ec) || ec) continue;
        for (const auto& path : Children(root, bank, remaining, limited)) {
            const auto index = path.stem().string();
            if (Lower(path.extension().string()) == ".png" && !index.empty() &&
                std::all_of(index.begin(), index.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) && fs::is_regular_file(path, ec)) files.push_back(path);
            if (files.size() >= kMaxCandidateFiles) break;
        }
        if (files.size() >= kMaxCandidateFiles || !remaining) break;
    }
    // Screen tiles are useful for a pack that changes only the front end. Normal
    // and roughness maps are never presented as if they were color artwork.
    if (files.size() < kMaxCandidateFiles) for (const auto& path : Children(root, root/"bmp", remaining, limited)) {
        if (Lower(path.extension().string()) == ".png" && fs::is_regular_file(path, ec)) files.push_back(path);
        if (files.size() >= kMaxCandidateFiles) break;
    }
    return files;
}
port::PngImage Thumbnail(port::PngImage image) {
    const float scale=std::min({1.f,float(kWidth)/image.width,float(kHeight)/image.height});
    if (scale==1) return image;
    port::PngImage thumb;
    thumb.width=std::max(1,static_cast<int>(std::round(image.width*scale)));
    thumb.height=std::max(1,static_cast<int>(std::round(image.height*scale)));
    thumb.rgba.resize(static_cast<size_t>(thumb.width)*thumb.height*4);
    for (int y=0;y<thumb.height;++y) for (int x=0;x<thumb.width;++x) {
        const int sx=std::min(image.width-1,static_cast<int>((x+.5f)*image.width/thumb.width));
        const int sy=std::min(image.height-1,static_cast<int>((y+.5f)*image.height/thumb.height));
        std::memcpy(thumb.rgba.data()+(static_cast<size_t>(y)*thumb.width+x)*4,
                    image.rgba.data()+(static_cast<size_t>(sy)*image.width+sx)*4,4);
    }
    return thumb;
}
void BlitFit(const port::PngImage& image, port::RgbaImage& sheet, int x, int y, int width, int height) {
    const float scale = std::min(float(width)/image.width, float(height)/image.height);
    const int w = std::max(1, static_cast<int>(std::round(image.width*scale)));
    const int h = std::max(1, static_cast<int>(std::round(image.height*scale)));
    x += (width-w)/2; y += (height-h)/2;
    for (int row=0; row<h; ++row) for (int col=0; col<w; ++col) {
        const int sx = std::min(image.width-1, static_cast<int>((col+.5f)*image.width/w));
        const int sy = std::min(image.height-1, static_cast<int>((row+.5f)*image.height/h));
        const uint8_t* src = image.rgba.data()+(static_cast<size_t>(sy)*image.width+sx)*4;
        uint8_t* dst = sheet.pixels.data()+(static_cast<size_t>(y+row)*sheet.width+x+col)*4;
        for (int c=0; c<3; ++c) dst[c] = static_cast<uint8_t>((int(src[c])*src[3]+int(dst[c])*(255-src[3])+127)/255);
    }
}
} // namespace

std::unique_ptr<PackPreview::Entry> PackPreview::Load(const fs::path& selected) {
    auto entry = std::make_unique<Entry>();
    auto& info = entry->info;
    info.original = false;
    info.caption = "Installed texture samples";
    std::error_code ec;
    const auto root = fs::weakly_canonical(selected, ec);
    info.name = selected.filename().string();
    if (ec || !fs::is_directory(root, ec) || ec) { info.message = "Pack directory is unavailable"; return entry; }
    std::string preview;
    const auto manifest = root/"pack.ini";
    if (fs::is_regular_file(manifest, ec)) {
        if (!Inside(root, manifest) || fs::file_size(manifest, ec)>16384 || ec) { info.message = "Pack manifest is outside the pack or exceeds 16 KiB"; return entry; }
        std::ifstream stream(manifest);
        for (std::string line; std::getline(stream, line);) {
            if (line.rfind("\xEF\xBB\xBF",0)==0) line.erase(0,3);
            line = Trim(std::move(line));
            if (line.empty() || line[0]=='#' || line[0]==';' || line[0]=='[') continue;
            const auto equals = line.find('='); if (equals==std::string::npos) continue;
            const auto key = Lower(Trim(line.substr(0,equals))), value = Trim(line.substr(equals+1));
            if (key=="name" && !value.empty()) info.name=Label(value);
            else if (key=="author") info.author=Label(value);
            else if (key=="version") info.version=Label(value);
            else if (key=="preview") preview=value;
        }
        if (!stream.eof()) { info.message="Cannot read pack manifest"; return entry; }
    }
    std::vector<port::PngImage> images;
    if (!preview.empty()) {
        fs::path path; port::PngImage image; std::string error;
        if (ExplicitPath(root, preview, &path) && Decode(root, path, &image, &error)) {
            info.explicitArtwork=true; info.caption="Pack preview artwork";
            info.sources.push_back(path.lexically_relative(root).generic_string()); images.push_back(Thumbnail(std::move(image)));
        } else info.message="Preview artwork unavailable; showing texture samples";
    }
    if (images.empty()) {
        size_t attempts=0;
        for (const auto& path : Candidates(root,&info.scanLimited)) {
            if (++attempts>8 || images.size()>=4) break;
            port::PngImage image; std::string error;
            if (Decode(root,path,&image,&error)) { info.sources.push_back(path.lexically_relative(root).generic_string()); images.push_back(Thumbnail(std::move(image))); }
        }
    }
    if (images.empty()) { info.message=info.scanLimited ? "Preview listing limit reached; add preview=artwork.png to pack.ini" : "No decodable artwork within preview limits"; return entry; }
    auto& sheet=entry->image;
    sheet.width=kWidth; sheet.height=kHeight; sheet.pixels.resize(kWidth*kHeight*4);
    for (size_t p=0;p<sheet.pixels.size();p+=4) { sheet.pixels[p]=12;sheet.pixels[p+1]=16;sheet.pixels[p+2]=18;sheet.pixels[p+3]=255; }
    if (images.size()==1) BlitFit(images[0],sheet,2,2,kWidth-4,kHeight-4);
    else for (size_t i=0;i<images.size();++i) BlitFit(images[i],sheet,2+int(i%2)*128,2+int(i/2)*96,124,92);
    return entry;
}

bool PackPreview::Select(const fs::path& path) {
    // Compare the caller's selection before canonicalization; no file stat is
    // needed on subsequent frames. Cache entries deliberately remain immutable.
    const std::string identity=Identity(path);
    if (haveSelection_ && selectedPath_==identity) return false;
    selectedPath_=identity;haveSelection_=true;
    if (path.empty()) { empty_.info.name="Original";empty_.info.caption="Original game textures";selected_=&empty_;return true; }
    auto found=cache_.find(identity);
    if (found!=cache_.end()) { selected_=found->second.get();return true; }
    if (cache_.size()>=64) { overflow_.info.original=false;overflow_.info.name=path.filename().string();overflow_.info.message="Preview cache is full; restart to preview more packs";selected_=&overflow_;return true; }
    auto entry=Load(path);selected_=entry.get();cache_.emplace(identity,std::move(entry));return true;
}
const PackPreviewInfo& PackPreview::Info() const { return selected_ ? selected_->info : empty_.info; }
const port::RgbaImage* PackPreview::Image() const { return selected_ && !selected_->image.pixels.empty() ? &selected_->image : nullptr; }
void PackPreview::AppendSprites(std::vector<port::ModelRenderer::SpriteDraw>& out,float x,float y,float width,float height,float horizontalScale) const {
    const auto* image=Image();
    if (!image || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(horizontalScale) || width<=0 || height<=0 || horizontalScale<=0) return;
    const float scale=std::min(width/image->width,height/image->height);
    const float w=image->width*scale,h=image->height*scale;
    const float x0=320+(x+(width-w)/2-320)*horizontalScale,x1=x0+w*horizontalScale;
    const float y0=y+(height-h)/2,y1=y0+h;
    port::ModelRenderer::SpriteDraw s{};s.texture=image;s.linear=true;s.hud=true;
    s.depth=.0045f;s.key=0x8000000F;s.isp=0xE4000000;s.tsp=0x94918000;
    s.v[0]={x0,y0,0,1,0xffffffff,0,0,0};s.v[1]={x0,y1,0,1,0xffffffff,0,0,1};
    s.v[2]={x1,y0,0,1,0xffffffff,0,1,0};s.v[3]={x1,y1,0,1,0xffffffff,0,1,1};out.push_back(s);
}
void PackPreview::ResetAfterRendererCacheClear() { cache_.clear();selected_=nullptr;selectedPath_.clear();haveSelection_=false; }
} // namespace play
