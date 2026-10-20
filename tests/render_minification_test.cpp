// World-texture minification must stay stable under subpixel camera motion,
// while sprites keep their original level-zero point sampling.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "render/asset_store.h"
#include "render/d3d11_device.h"
#include "render/texture_mips.h"
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static port::RgbaImage Image(int width, int height) {
    port::RgbaImage image;
    image.width = width; image.height = height;
    image.pixels.resize(static_cast<size_t>(width * height * 4), 0);
    return image;
}

static void CheckMipPixels() {
    auto odd = Image(3,5);
    for (int y=0;y<5;++y) for (int x=0;x<3;++x) {
        auto* p = odd.pixels.data() + (y*3+x)*4;
        p[0] = p[1] = p[2] = y==4 ? 255 : 0; p[3] = 255;
    }
    const auto base = odd.pixels;
    const auto levels = port::BuildTextureMips(odd);
    Require(odd.pixels==base,"mip generation modified the original level");
    Require(levels.size()==2 && levels[0].width==1 && levels[0].height==2 &&
            levels[1].width==1 && levels[1].height==1,"odd dimensions do not terminate at 1x1");
    Require(std::abs(int(levels[0].pixels[4])-85)<=1,"odd-size mip dropped the bottom source row");
    auto narrow = Image(1,7);
    for (size_t i=3;i<narrow.pixels.size();i+=4) narrow.pixels[i]=255;
    const auto narrowLevels = port::BuildTextureMips(narrow);
    Require(narrowLevels.size()==2 && narrowLevels[0].width==1 && narrowLevels[0].height==3 &&
            narrowLevels[1].width==1 && narrowLevels[1].height==1,"1xN mip dimensions are invalid");
    auto alpha = Image(2,2);
    for (int i=0;i<4;++i) alpha.pixels[i*4+2]=255; // invisible blue must not bleed into red
    alpha.pixels[0]=255; alpha.pixels[2]=0; alpha.pixels[3]=255;
    const auto alphaLevels = port::BuildTextureMips(alpha);
    const auto& p = alphaLevels[0].pixels;
    Require(p[0]==255 && p[1]==0 && p[2]==0 && std::abs(int(p[3])-64)<=1,
            "transparent RGB contaminated the visible mip colour");
    alpha.pixels[3]=0;
    const auto transparent = port::BuildTextureMips(alpha);
    Require(transparent[0].pixels==std::vector<uint8_t>({0,0,0,0}),"fully transparent mip retained stray RGB");
    const auto opaque = port::BuildTextureMips(alpha,false);
    Require(std::abs(int(opaque[0].pixels[0])-64)<=1 && opaque[0].pixels[1]==0 &&
            std::abs(int(opaque[0].pixels[2])-191)<=1 && opaque[0].pixels[3]==0,
            "opaque-texture mips discarded visible RGB behind unused alpha");
}

static void Identity(float m[16]) {
    std::fill(m,m+16,0.f);
    m[0]=m[5]=m[10]=m[15]=1;
}

static std::array<game::ModelVertex,6> Quad() {
    std::array<game::ModelVertex,6> v{};
    const float corners[6][4]={{-.5f,-.5f,0,1},{.5f,-.5f,1,1},{-.5f,.5f,0,0},
                              {-.5f,.5f,0,0},{.5f,-.5f,1,1},{.5f,.5f,1,0}};
    for (int i=0;i<6;++i) {
        v[i].pos[0]=corners[i][0]; v[i].pos[1]=corners[i][1]; v[i].pos[2]=-4;
        v[i].normal[2]=1; v[i].uv[0]=corners[i][2]; v[i].uv[1]=corners[i][3];
    }
    return v;
}

static void SpriteQuad(std::vector<game::TLVertex>& out,float x,float y,float width,float height) {
    // The sprite shader uses the game's 640x480 coordinates; the target is 128x128.
    for (auto p : {std::array<float,4>{x,y,0,0},{x,y+height,0,1},{x+width,y,1,0},{x+width,y+height,1,1}}) {
        game::TLVertex vertex{};
        vertex.x=p[0]; vertex.y=p[1]; vertex.z=.5f; vertex.rhw=1;
        vertex.diffuse=0xffffffffu; vertex.u=p[2]; vertex.v=p[3];
        out.push_back(vertex);
    }
}

int main(int argc,char** argv) {
    CheckMipPixels();
    std::string error;
    port::GpuDeviceDesc desc;
    desc.width=desc.height=128;
    std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if (argc>1 && std::strcmp(argv[1],"sdlgpu")==0) {
        Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());
        gpu=port::CreateSdlGpuDevice(desc,&error);
    } else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());
    auto checker=Image(256,256);
    for (int y=0;y<256;++y) for (int x=0;x<256;++x) {
        auto* p=checker.pixels.data()+(y*256+x)*4;
        p[0]=p[1]=p[2]=((x+y)&1)?255:0; p[3]=255;
    }
    auto glyph=Image(8,8);
    for (int y=0;y<8;++y) for (int x=0;x<8;++x) {
        auto* p=glyph.pixels.data()+(y*8+x)*4;
        const bool stroke=(y>=1 && y<=6 && (x==1 || x==6)) || (y==3 && x>=1 && x<=6);
        p[0]=p[1]=p[2]=stroke?255:0; p[3]=255;
    }
    const auto checkerTexture=gpu->CreateTexture(checker), glyphTexture=gpu->CreateTexture(glyph);
    Require(checkerTexture!=0 && glyphTexture!=0,"create test textures");
    const auto vertices=Quad();
    gpu->SetStaticVertices(vertices.data(),vertices.size());
    std::vector<game::TLVertex> spriteVertices;
    SpriteQuad(spriteVertices,520,390,40,30); // 8x8 native-size glyph at target (104,104)
    SpriteQuad(spriteVertices,440,30,160,120); // 32x32 minified HUD negative control at (88,8)
    gpu->SetSpriteVertices(spriteVertices.data(),spriteVertices.size());
    std::vector<uint8_t> hudReference;
    float legacyMin=255,legacyMax=0,mipMin=255,mipMax=0;
    for (bool mipmapped : {false,true}) for (bool linear : {false,true}) {
        // The point-filtered legacy case supplies a strong aliasing negative control.
        // The corrected minification must be stable for both authored MAG modes.
        if (!mipmapped && linear) continue;
        for (int phase=0;phase<16;++phase) {
            port::GpuDrawList list;
            list.width=list.height=128;
            port::ModelConstants c{};
            Identity(c.world);
            c.world[12]=((phase+.15f)/16.f)/32.f; // less than one target pixel of camera-relative motion
            c.viewProj[0]=c.viewProj[5]=2; c.viewProj[11]=-1; c.viewProj[14]=2; // perspective W=-Z
            for (float& value:c.diffuse) value=1;
            c.flags[0]=c.flags[1]=c.flags[3]=1; c.flags[2]=-1;
            list.model.push_back(c);
            port::GpuDraw world;
            world.count=6; world.texture=checkerTexture;
            world.state.linear=linear?1:0; world.state.mipmapped=mipmapped?1:0;
            list.draws.push_back(world);
            port::SpriteConstants sc{};
            sc.screen[0]=128.f/640; sc.screen[1]=128.f/480; sc.screen[2]=-1; sc.screen[3]=1;
            list.sprite.push_back(sc);
            for (int i=0;i<2;++i) {
                port::GpuDraw hud;
                hud.pipeline=port::GpuDraw::Pipeline::Sprite; hud.stream=port::GpuDraw::Stream::Sprite;
                hud.first=i*4; hud.count=4; hud.texture=i==0?glyphTexture:checkerTexture;
                hud.state.zfunc=8; hud.state.zwrite=0; hud.state.addrU=hud.state.addrV=3;
                Require(hud.state.mipmapped==0,"sprite default no longer clamps to level zero");
                list.draws.push_back(hud);
            }
            const float clear[4]={0,0,0,1};
            Require(gpu->BeginFrame(clear),"begin minification frame");
            gpu->Submit(list);
            std::vector<uint8_t> pixels;
            Require(gpu->ReadPixels(&pixels),"read minification pixels");
            gpu->EndFrame();
            float mean=0;
            for (int y=56;y<72;++y) for (int x=56;x<72;++x) {
                const auto* p=pixels.data()+(y*128+x)*4;
                mean+=p[0]/256.f;
                if (mipmapped) for (int k=0;k<3;++k)
                    Require(std::abs(int(p[k])-128)<=2,"minified checker did not converge to stable grey");
            }
            if (mipmapped) {mipMin=std::min(mipMin,mean);mipMax=std::max(mipMax,mean);}
            else {legacyMin=std::min(legacyMin,mean);legacyMax=std::max(legacyMax,mean);}
            std::vector<uint8_t> hudPixels;
            for (int y=104;y<112;++y) for (int x=104;x<112;++x) {
                const auto* p=pixels.data()+(y*128+x)*4;
                const auto* expected=glyph.pixels.data()+((y-104)*8+x-104)*4;
                for (int k=0;k<3;++k) Require(p[k]==expected[k],"one-pixel HUD glyph was blurred or changed");
                hudPixels.insert(hudPixels.end(),p,p+4);
            }
            // This sprite reuses the world's mip-capable texture at 8:1 minification.
            // If its sampler inherits world mips, it becomes grey rather than level-zero black/white.
            for (int y=20;y<28;++y) for (int x=100;x<108;++x) {
                const auto* p=pixels.data()+(y*128+x)*4;
                for (int k=0;k<3;++k) Require(p[k]<=1 || p[k]>=254,"HUD minification sampled a generated mip level");
                hudPixels.insert(hudPixels.end(),p,p+4);
            }
            if (hudReference.empty()) hudReference=hudPixels;
            else Require(hudPixels==hudReference,"world filtering or camera phase leaked into HUD sampling");
        }
    }
    // Close-up 3D textures keep the authored magnification mode. This is a
    // different contract from the sprite sampler: the same perspective quad now
    // enlarges the 8x8 glyph to 32x32 pixels, with model mips toggled in place.
    for (bool linear : {false,true}) {
        std::vector<uint8_t> magnifiedReference;
        for (bool mipmapped : {false,true}) {
            port::GpuDrawList list;
            list.width=list.height=128;
            port::ModelConstants c{};
            Identity(c.world);
            c.viewProj[0]=c.viewProj[5]=2; c.viewProj[11]=-1; c.viewProj[14]=2;
            for (float& value:c.diffuse) value=1;
            c.flags[0]=c.flags[1]=c.flags[3]=1; c.flags[2]=-1;
            list.model.push_back(c);
            port::GpuDraw draw;
            draw.count=6; draw.texture=glyphTexture;
            draw.state.linear=linear?1:0; draw.state.mipmapped=mipmapped?1:0;
            list.draws.push_back(draw);
            const float clear[4]={0,0,0,1};
            Require(gpu->BeginFrame(clear),"begin magnification frame");
            gpu->Submit(list);
            std::vector<uint8_t> pixels;
            Require(gpu->ReadPixels(&pixels),"read magnification pixels");
            gpu->EndFrame();
            if (magnifiedReference.empty()) magnifiedReference=pixels;
            else for (size_t i=0;i<pixels.size();i+=4)
                Require(std::memcmp(pixels.data()+i,magnifiedReference.data()+i,3)==0,
                        "mip opt-in changed close-up world texture magnification");
        }
    }
    // Some original opaque materials use RGB from texels whose alpha is zero.
    // Their mip chain must average straight RGB, rather than replacing it with
    // black as a coverage-weighted transparent texture would do.
    auto opaqueChecker=checker;
    for (size_t i=3;i<opaqueChecker.pixels.size();i+=4) opaqueChecker.pixels[i]=0;
    const auto opaqueTexture=gpu->CreateTexture(opaqueChecker,false);
    Require(opaqueTexture!=0,"create opaque texture with unused zero alpha");
    {
        port::GpuDrawList list;
        list.width=list.height=128;
        port::ModelConstants c{};
        Identity(c.world);
        c.viewProj[0]=c.viewProj[5]=2; c.viewProj[11]=-1; c.viewProj[14]=2;
        for (float& value:c.diffuse) value=1;
        c.flags[0]=c.flags[1]=c.flags[3]=1; c.flags[2]=-1; // no alpha test or blending on opaque RGB
        list.model.push_back(c);
        port::GpuDraw draw;
        draw.count=6; draw.texture=opaqueTexture; draw.state.mipmapped=1;
        list.draws.push_back(draw);
        const float clear[4]={0,0,0,1};
        Require(gpu->BeginFrame(clear),"begin opaque alpha-zero minification frame");
        gpu->Submit(list);
        std::vector<uint8_t> pixels;
        Require(gpu->ReadPixels(&pixels),"read opaque alpha-zero minification pixels");
        gpu->EndFrame();
        for (int y=56;y<72;++y) for (int x=56;x<72;++x) {
            const auto* p=pixels.data()+(y*128+x)*4;
            for (int k=0;k<3;++k) Require(std::abs(int(p[k])-128)<=2,
                                        "opaque alpha-zero texture turned black when minified");
        }
    }
    std::printf("%s checker mean range: legacy %.1f..%.1f, mipmapped %.1f..%.1f\n",gpu->Name(),legacyMin,legacyMax,mipMin,mipMax);
    Require(legacyMax-legacyMin>=200,"legacy negative control did not expose minification shimmer");
    Require(mipMax-mipMin<=2,"mipmapped world texture shimmered under subpixel motion");
    std::puts("PASS world minification stable; point HUD sampling and odd/alpha mip pixels retained");
}
