// Port-only, authored HOTD4 chapter-one adaptation. HOTD2 supplies the materials,
// character skeletons, motions, sounds and HUD font; none of its event scripts run here.
#include "game/world_prototype.h"
#include "game/world_modes.h"
#include "game/world_menu_pointer.h"
#include "game/mstack.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace game {
namespace {
struct Point { float x=0, y=0, z=0; };
enum Phase { Intro, Travel, Fight, Fork, Grab, Boss, Clear, Defeat };
struct Body {
    std::unique_ptr<EnemyTask> actor;
    int hp=90, maxHp=90, death=0, attack=0, flash=0;
    bool boss=false;
    float speed=.09f;
    float sx=320, sy=240, radius=20, headX=320, headY=200, headRadius=8;
};
struct State : TaskData {
    Phase phase=Intro;
    int tick=0, age=0, encounter=0, route=-1, pending=0, spawnClock=0;
    int kills=0, shots=0, hits=0, grenades[2]={3,3}, cooldown[2]={}, reload[2]={};
    int invincible[2]={}, blast=0, grabbed=0, escape=0, bossClock=0, bossDamage=0;
    int bossCycle=0, returnPhase=Fight;
    float door=0;
    int yaw=0;
    Point camera{0,18,55}, from{0,18,55};
    std::vector<Body> bodies;
    std::string message;
};
State& S(World::Impl& w) { return *static_cast<State*>(w.prototype.get()); }
const State& S(const World::Impl& w) { return *static_cast<const State*>(w.prototype.get()); }
constexpr float gold[3]={1,.79f,.35f}, red[3]={1,.32f,.26f}, blue[3]={.5f,.8f,1};
constexpr const char* names[] = {"CLASSIFIED ARCHIVE", "BASEMENT CORRIDOR", "SECURITY CHECKPOINT", "STAIRWELL",
    "UPPER OFFICES", "ELEVATOR BALCONY", "LOWER MAINTENANCE", "SERVICE PASSAGE", "ELEVATOR HALL", "JUSTICE"};
// Rooms form a route graph, with both stair branches rejoining before the lift.
Point Centre(int e) {
    static constexpr Point p[]={{0,0,0},{0,0,-180},{0,0,-360},{0,0,-540},
        {-110,28,-720},{-110,28,-900},{110,-28,-720},{110,-28,-900},{0,0,-1080},{0,-70,-1300}};
    return p[std::clamp(e,0,9)];
}
Point Eye(int e) { Point p=Centre(e); p.y+=18; p.z+=48; return p; }
void Text(World::Impl& w, float x,float y,float scale,const std::string& s,const float* color=nullptr,bool centre=true) {
    PortGlyphText(w,x,y,scale,s,.82f,centre,color);
}
void Box(World::Impl& w,int texture,float x,float y,float z,float sx,float sy,float sz,int yaw=0) {
    Mat4 m=Mat4::Identity(); mx::Translate(m,x,y,z); mx::RotY(m,yaw); mx::Scale(m,sx,sy,sz);
    w.Draw(kPrototypeMaterialBase+texture,m,7);
}
void Floor(World::Impl& w,Point c,float width,float depth,int tex) {
    for(float x=-width/2+10;x<width/2;x+=20) for(float z=-depth/2+10;z<depth/2;z+=20)
        Box(w,tex,c.x+x,c.y-1,c.z+z,20,2,20);
}
void Room(World::Impl& w,State& h,int e) {
    const Point c=Centre(e); const bool arena=e==9; const float width=arena?180:110, height=arena?80:38;
    Floor(w,c,width,160,e>=6&&e<=7?39:44);
    for(int side : {-1,1}) for(int i=0;i<8;++i) {
        const float z=c.z-70+i*20;
        Box(w,e==0?23:e>=6&&e<=7?46:30,c.x+side*width/2,c.y+height/2,z,2,height,20);
        Box(w,37,c.x+side*(width/2-1),c.y+4,z,1,5,20);
        if(i%2==0) {
            Box(w,19,c.x+side*(width/2-2),c.y+27,z,2,6,10);
            Box(w,14,c.x+side*(width/2-4),c.y+height-2,z,5,2,14);
        }
    }
    // Concrete lintels and open central doorways; doors slide before each advance.
    for(int side : {-1,1}) Box(w,21,c.x+side*(width/4+11),c.y+height/2,c.z-79,width/2-22,height,3);
    Box(w,23,c.x,c.y+height-4,c.z-79,44,8,3);
    if(!arena) {
        for(int i=0;i<8;++i) Box(w,39,c.x,c.y+height,c.z-70+i*20,width,1,20);
        for(int i=0;i<4;++i) Box(w,28,c.x,c.y+height-1,c.z-60+i*40,18,1,5);
    }
    const float open=e<h.encounter || (e==h.encounter && h.phase==Travel)?1.0f:e==h.encounter?h.door:0.0f;
    if(e!=3 && !arena) for(int side : {-1,1})
        Box(w,24,c.x+side*(11+22*open),c.y+15,c.z-78,22,30,2);
    if(e==3) Box(w,24,c.x,c.y+15,c.z-78,44,30,2); // closed centre door; the stairs are the two exits
    if(e==0) { // archive shelves and cabinets, built from HOTD2's original cabinet/vent panels
        for(int side : {-1,1}) for(int i=0;i<3;++i) {
            Box(w,17,c.x+side*45,c.y+13,c.z-45+i*32,14,26,20);
            for(int row=0;row<3;++row) Box(w,18,c.x+side*37.5f,c.y+5+row*8,c.z-45+i*32,1,6,17);
        }
        Box(w,14,c.x-25,c.y+8,c.z+32,28,2,14);
        Box(w,15,c.x-25,c.y+12,c.z+29,9,7,3);
    }
    if(e==4) for(int side:{-1,1}) for(int i=0;i<3;++i) {
        const float x=c.x+side*42,z=c.z-45+i*38;
        Box(w,14,x,c.y+8,z,18,2,25);
        for(int leg:{-1,1}) Box(w,20,x+leg*6,c.y+4,z,2,8,20);
        Box(w,15,x,c.y+13,z-5,10,9,3);
        Box(w,16,x+side*6,c.y+5,z+6,5,10,10);
    }
    if(e==2) {
        Box(w,17,c.x-40,c.y+13,c.z-35,18,26,8);
        Box(w,34,c.x-39,c.y+18,c.z-30,11,10,1);
        Box(w,38,c.x+44,c.y+12,c.z-38,10,24,10);
    }
    if(e==3) for(int side : {-1,1}) {
        for(int i=0;i<12;++i) Box(w,39,c.x+side*(20+i*3),c.y+(side==-1?1:-1)*i*2.3f,
            c.z-12-i*5,28,3,6);
        Box(w,20,c.x+side*38,c.y+18,c.z-60,3,20,3);
    }
    if(e==5) for(int i=0;i<7;++i) Box(w,20,c.x+36,c.y+7,c.z-60+i*20,2,14,2);
    if(e==6 || e==7 || arena) {
        for(int side : {-1,1}) for(int i=0;i<3;++i)
            Box(w,37,c.x+side*(width/2-5-i*3),c.y+30+i*3,c.z,2,2,150);
        for(int i=0;i<3;++i) Box(w,35,c.x+35,c.y+7,c.z-40+i*35,18,14,16);
    }
    if(e==8) { // lift doors, control panel and pillars
        for(int side : {-1,1}) {
            Box(w,26,c.x+side*12,c.y+17,c.z-75,23,34,2);
            Box(w,21,c.x+side*38,c.y+19,c.z-58,8,38,8);
        }
        Box(w,34,c.x-32,c.y+15,c.z-72,5,10,3);
    }
    if(arena) {
        for(int i=0;i<9;++i) Box(w,31,c.x-65+(i*29)%130,c.y+2,c.z-55+(i*37)%120,8+i%3*3,4,7,(i*6800)&65535);
        Box(w,12,c.x,c.y+28,c.z-79,44,56,3);
    }
}
void DrawEnvironment(World::Impl& w,State& h) {
    w.animLight=0; w.light={}; w.light.intensity=.85f; w.light.pitch=0x1800; w.light.yaw=0x5000;
    w.light.color[0]=.9f; w.light.color[1]=.94f; w.light.color[2]=1;
    w.env.fogNear=65535; w.env.fogFar=65536;
    // Render nearby graph nodes only. No original stage geometry or outdoor sky is submitted.
    for(int e=0;e<10;++e) {
        if(h.route==0 && (e==6||e==7)) continue;
        if(h.route==1 && (e==4||e==5)) continue;
        if(std::abs(Centre(e).z-h.camera.z)<380) Room(w,h,e);
    }
    const int branch=h.route==1?6:4;
    const int edge[8][2]={{0,1},{1,2},{2,3},{3,branch},{branch,branch+1},{branch+1,8},{8,9},{3,h.route==1?4:6}};
    for(const auto& pair:edge) {
        if (&pair == &edge[7] && h.route >= 0) continue;
        Point a=Centre(pair[0]), b=Centre(pair[1]);
        if(pair[0]==8 || std::abs(a.z-h.camera.z)>380) continue;
        // An enclosed stair connector between the rear doorway and the next room.
        a.z-=80; b.z+=80;
        const float dx=b.x-a.x,dz=b.z-a.z,len=std::max(1.f,std::hypot(dx,dz));
        const float rx=-dz/len,rz=dx/len;
        const int yaw=static_cast<int>(-std::atan2(dx,-dz)*mx::kToAngle);
        for(int i=0;i<20;++i) {
            float t=(i+.5f)/20; Point c{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
            Box(w,39,c.x,c.y-1,c.z,44,2,len/20+1,yaw);
            for(int side:{-1,1}) Box(w,23,c.x+side*23*rx,c.y+18,c.z+side*23*rz,2,38,len/20+1,yaw);
            Box(w,39,c.x,c.y+38,c.z,46,2,len/20+1,yaw);
        }
    }
    if(h.encounter>=8) for(int side:{-1,1})
        Box(w,46,side*56,-20,-1190,3,140,100);
}
void Camera(World::Impl& w,State& h) {
    w.cam={}; w.cam.pos[0]=h.camera.x; w.cam.pos[1]=h.camera.y; w.cam.pos[2]=h.camera.z;
    w.cam.inverse=Mat4::Identity(); mx::Translate(w.cam.inverse,h.camera.x,h.camera.y,h.camera.z);
    w.cam.yaw=h.yaw; mx::RotY(w.cam.inverse,h.yaw);
    w.cam.view=mx::Inverse(w.cam.inverse); w.view0=w.cam.view; w.inverse0=w.cam.inverse;
    memcpy(w.playerPos,w.cam.pos,12); w.playerPos[1]-=18;
    memcpy(w.camState.pos,w.cam.pos,12); w.camState.pitch=w.camState.roll=0; w.camState.yaw=h.yaw;
}
void Change(State& h,Phase phase) { h.phase=phase; h.tick=0; }
void BodyNode(World::Impl& w,EnemyTask& e,int idx,const Mat4& m) { w.Draw(e.anim.nodes[idx].model,m,8,-1,false,idx,true); }
void Spawn(World::Impl& w,State& h,bool boss=false) {
    Body b; b.actor=std::make_unique<EnemyTask>(); auto& e=*b.actor;
    const int index=h.kills+static_cast<int>(h.bodies.size())+h.pending;
    e.anim.kind=boss?0:(h.encounter>=2 && index%4==0?5:index%3==0?8:7);
    e.anim.motion[0]=w.EnemyMotionSet(e,0);
    w.EnemyInitPose(e);
    if(e.actorSlot>=0) w.actorSlots[e.actorSlot]=nullptr;
    e.actorSlot=-1; e.nodeFn=BodyNode; e.flags34=0x40000; e.anim.drawFlags=1; e.anim.rotOrder=1;
    const Point c=Centre(h.encounter);
    e.pos[0]=c.x+(boss?0:float((h.pending*31)%57-28)); e.pos[1]=c.y; e.pos[2]=c.z-(boss?65:40+(h.pending%3)*9);
    e.rot[1]=0x8000;
    b.boss=boss; b.hp=b.maxHp=boss?2200:(e.anim.kind==5?140:70);
    e.anim.scale=boss?2.7f:1.0f; b.speed=e.anim.kind==5?.07f:.10f+float(index%4)*.008f;
    h.bodies.push_back(std::move(b));
}
void Project(World::Impl& w,const float pos[3],float& x,float& y,float& depth) {
    float v[3]; mx::Transform(w.cam.view,pos,v); depth=std::max(1.0f,-v[2]);
    x=320+v[0]*w.focal/depth; y=240-v[1]*w.focal/depth;
}
void DrawBodies(World::Impl& w,State& h,bool animate) {
    for(auto& b:h.bodies) {
        auto& e=*b.actor;
        if(b.death>0 && h.phase==Clear) ++b.death;
        if(b.death>0) { e.rot[0]=std::min(b.death,30)*-500; e.pos[1]=Centre(h.encounter).y-std::min(b.death,90)*.08f; }
        else if(b.attack>30) e.rot[0]=-std::min(1500,(b.attack-30)*30); // wind-up before a close-range strike
        else e.rot[0]=0;
        w.AnimDraw(e);
        if(animate) {
            ++e.anim.counter[0];
            if(e.anim.ended) w.AnimSetMotion(e,w.EnemyMotionSet(e,0));
        }
        float depth=1; float head[3]={e.pos[0],e.pos[1]+17*e.anim.scale,e.pos[2]};
        // Node 2 is the original skeleton's head; use its animated centre for hit testing and the marker.
        if(e.anim.nodes[2].used) for(int k=0;k<3;++k) head[k]=e.anim.nodes[2].world.m[12+k];
        Project(w,head,b.headX,b.headY,depth); b.headRadius=std::max(6.0f,2.1f*e.anim.scale*w.focal/depth);
        float body[3]={e.pos[0],e.pos[1]+9*e.anim.scale,e.pos[2]};
        Project(w,body,b.sx,b.sy,depth); b.radius=std::max(10.0f,6*e.anim.scale*w.focal/depth);
        if(b.boss && !b.death) {
            // Four-arm silhouette using a second pair of the original brute's arm nodes.
            for(int n:{3,4,5,6,7,8}) if(e.anim.nodes[n].model) {
                Mat4 m=e.anim.nodes[n].world; m.m[13]-=10; m.m[14]+=3;
                w.Draw(e.anim.nodes[n].model,m,8);
            }
            if(h.bossClock%360>=100 && h.bossClock%360<275) {
                PortSprite(w,0xB6,b.headX-8,b.headY-3,.81f,1,.6f,0,0x705);
                Text(w,b.headX,b.headY+b.headRadius+12,.4f,"WEAK POINT",red);
            }
        }
    }
}
void Kill(World::Impl& w,State& h,Body& b,int p) {
    if(b.hp>0 || b.death) return;
    b.death=1; ++h.kills; w.player[p].score+=b.boss?5000:100;
    w.Sound(0x1917A9);
}
void Hurt(World::Impl& w,State& h,int p) {
    if(w.player[p].state!=5 || h.invincible[p]>0) return;
    --w.player[p].lives; w.player[p].livesShown=w.player[p].lives; h.invincible[p]=100;
    w.Sound(0x1A16A9);
    if(w.player[p].lives<=0) w.player[p].state=9;
    if(w.player[0].state!=5 && w.player[1].state!=5) { Change(h,Defeat); w.Sound(0x10000009); }
}
void Next(World::Impl& w,State& h) {
    h.bodies.clear(); h.from=h.camera;
    if(h.encounter==3) { Change(h,Fork); return; }
    if(h.encounter==5 || h.encounter==7) h.encounter=8;
    else if(h.encounter==8) {
        h.encounter=9; Change(h,Grab); h.returnPhase=Travel; h.escape=0; h.grabbed=0;
        Spawn(w,h,true); // the lift guardian grabs the agents before the floor collapses
        auto& e=*h.bodies.back().actor; e.pos[1]=h.camera.y-18; e.pos[2]=h.camera.z-90;
        return;
    }
    else ++h.encounter;
    h.door=1; Change(h,Travel); w.Sound(0x121A9);
}
void StartFight(World::Impl& w,State& h) {
    h.door=0; h.bodies.clear(); h.spawnClock=0;
    if(h.encounter==9) { h.pending=0; h.bossClock=0; Spawn(w,h,true); Change(h,Boss); w.Sound(0x10000005); return; }
    const int counts[]={6,8,10,8,12,16,14,16,22};
    h.pending=counts[h.encounter]; Change(h,Fight);
}
void Combat(World::Impl& w,State& h) {
    int alive=0;
    for(auto& b:h.bodies) if(!b.death) ++alive;
    if(h.phase==Fight && h.pending>0 && alive<6 && --h.spawnClock<=0) {
        --h.pending; Spawn(w,h); h.spawnClock=45; ++alive;
    }
    for(auto& b:h.bodies) {
        if(b.death) { ++b.death; continue; }
        auto& e=*b.actor;
        if(!b.boss) {
            const float dz=h.camera.z-14-e.pos[2];
            if(dz>0) e.pos[2]+=b.speed;
            if(dz<13 && ++b.attack>=90) { Hurt(w,h,w.player[0].state==5?0:1); b.attack=0; }
        } else {
            ++h.bossClock;
            int cycle=h.bossClock%360;
            e.pos[0]=Centre(9).x+std::sin(h.bossClock*.008f)*22;
            e.pos[2]=Centre(9).z-65;
            if(cycle==275) {
                if(h.bossDamage<120) Hurt(w,h,w.player[0].state==5?0:1);
                h.bossDamage=0;
            }
            if(cycle==355 && ++h.bossCycle%2==0) { Change(h,Grab); h.returnPhase=Boss; h.escape=0; h.grabbed=0; }
        }
    }
    h.bodies.erase(std::remove_if(h.bodies.begin(),h.bodies.end(),[](const Body& b){return b.death>90;}),h.bodies.end());
    if(alive==0 && h.pending==0 && h.tick>60) {
        if(h.phase==Boss) { Change(h,Clear); w.Sound(0x10000003); }
        else if(h.phase==Fight) Next(w,h);
    }
}
void Controls(World::Impl& w,State& h) {
    for(int p=0;p<2;++p) {
        auto& P=w.player[p]; const uint32_t held=(w.held>>(p*16))&0xFFF, edge=(w.pressed>>(p*16))&0xFFF;
        if(P.state!=5) {
            if(p==1 && (edge&0xC) && h.phase<Clear) { P.state=5; P.lives=P.livesShown=5; P.ammo=30; }
            continue;
        }
        P.aim[0]=P.gun.x; P.aim[1]=P.gun.y; P.crosshairOn=true;
        P.crosshair[0]=P.aim[0]; P.crosshair[1]=P.aim[1];
        if(h.invincible[p]>0) --h.invincible[p];
        if(h.cooldown[p]>0) --h.cooldown[p];
        if(h.reload[p]>0 && --h.reload[p]==0) { P.ammo=30; P.empty=0; }
        if((edge&2) && !h.reload[p] && P.ammo<30) { h.reload[p]=28; w.Sound(0x3E16A9); }
        if(h.phase==Intro || h.phase==Travel || h.phase==Clear || h.phase==Defeat) continue;
        if((edge&kPrototypeGrenade) && h.grenades[p]>0 && (h.phase==Fight || h.phase==Boss || h.phase==Grab)) {
            --h.grenades[p]; h.blast=24; w.Sound(0xB16A9);
            if(h.phase==Grab) h.escape=18;
            for(auto& b:h.bodies) if(!b.death) {
                const int damage=b.boss ? (h.bossClock%360>=100 && h.bossClock%360<275?350:35):160;
                b.hp-=damage; h.bossDamage+=damage; Kill(w,h,b,p);
            }
        }
        if(!(held&4) || h.cooldown[p] || h.reload[p] || P.ammo==0 || !P.gun.valid) continue;
        h.cooldown[p]=5; --P.ammo; ++P.shots; ++h.shots; P.firedThisFrame=true; P.empty=P.ammo==0;
        uint8_t soundRow=0; w.exe.Read(0x004EC928+2*8+2,&soundRow,1); // Original-mode machine-gun sound
        w.Sound(w.exe.U32(0x004EC9A0+4*soundRow));
        const float x=320+P.aim[0],y=240-P.aim[1];
        if(h.phase==Fork) {
            if(y>150 && y<340) { h.route=x<320?0:1; h.encounter=h.route==0?4:6; h.from=h.camera; Change(h,Travel); }
            continue;
        }
        if(h.phase==Grab) { ++h.escape; continue; }
        Body* hit=nullptr; float best=1e9f; bool head=false;
        for(auto& b:h.bodies) if(!b.death) {
            bool hh=std::hypot(x-b.headX,y-b.headY)<b.headRadius*1.3f;
            bool body=std::abs(x-b.sx)<b.radius && std::abs(y-b.sy)<b.radius*1.45f;
            float depth=h.camera.z-b.actor->pos[2];
            if((hh||body)&&depth<best) { hit=&b; best=depth; head=hh; }
        }
        if(hit) {
            int damage=head?35:18;
            if(hit->boss) damage=head && h.bossClock%360>=100 && h.bossClock%360<275?35:2;
            hit->hp-=damage; hit->flash=4; ++h.hits; ++P.hits;
            if(hit->boss) h.bossDamage+=damage;
            Kill(w,h,*hit,p);
        }
        Mat4 flash=Mat4::Identity(); mx::Translate(flash,(x-320)/w.focal,(240-y)/w.focal,-1);
        mx::Scale(flash,.075f,.075f,.075f);
        w.Draw(w.exe.S16(0x00579F78+2*p),flash,0xC,-1,false,-1,true);
        // Shootable supply panels: one per checkpoint, marked on the screen.
        if((h.encounter==2 || h.encounter==8) && x<100 && y>160 && y<300 && h.message!="SUPPLY COLLECTED") {
            h.grenades[p]=std::min(5,h.grenades[p]+1); P.lives=P.livesShown=std::min(5,int(P.lives)+1);
            h.message="SUPPLY COLLECTED"; w.Sound(0x121A9);
        }
    }
}
void Hud(World::Impl& w,State& h) {
    Text(w,320,9,.65f,"PROTOTYPE  /  CHAPTER 1 - ESCAPE",gold);
    Text(w,320,32,.45f,names[h.encounter],blue);
    if(h.phase==Intro) {
        Text(w,320,155,.9f,"ESCAPE",gold);
        Text(w,320,205,.46f,"AMS BASEMENT - THE DOORS WILL NOT HOLD");
        Text(w,320,252,.45f,"HOLD FIRE - MACHINE GUN    G - GRENADE");
        Text(w,320,277,.45f,"RIGHT CLICK - RELOAD    ESC - PAUSE");
        Text(w,320,323,.4f,"HOTD4 CHAPTER ADAPTATION WITH HOTD2 ASSETS",blue);
    } else if(h.phase==Travel && (h.from.x!=Eye(h.encounter).x || h.encounter==9)) {
        const float opacity=std::min(1.f,std::min(h.tick/30.f,(120-h.tick)/30.f));
        Mat4 cover=Mat4::Identity(); mx::Translate(cover,0,0,-.0005f);
        w.Draw(0x93E,cover,8,std::max(0.f,opacity),false,-1,true);
        if(h.tick>30 && h.tick<90) Text(w,320,210,.55f,h.encounter==9?"THE FLOOR COLLAPSES":h.encounter==8?"TO THE ELEVATOR HALL":h.route?"DESCENDING TO MAINTENANCE":"ASCENDING TO THE OFFICES",gold);
    } else if(h.phase==Fork) {
        Text(w,320,123,.7f,"CHOOSE YOUR ROUTE",gold);
        Text(w,166,220,.65f,"UPSTAIRS"); Text(w,474,220,.65f,"DOWNSTAIRS");
        Text(w,320,310,.48f,"SHOOT A SIDE OR PRESS LEFT / RIGHT");
        Text(w,320,345,.45f,"TIME "+std::to_string(std::max(0,(600-h.tick+59)/60)),gold);
    } else if(h.phase==Grab) {
        Text(w,320,160,.85f,"BREAK FREE",red);
        Text(w,320,212,.5f,"HOLD FIRE OR USE A GRENADE");
        Text(w,320,255,.65f,std::to_string(std::min(100,h.escape*100/18))+" PERCENT",gold);
    } else if(h.phase==Boss) {
        Text(w,320,57,.55f,"JUSTICE - TYPE 0053",red);
        for(auto& b:h.bodies) if(b.boss && !b.death) {
            PortSprite(w,0xB6,180,83,.82f,std::max(0,b.hp)*280.f/b.maxHp/16,.5f,0,0x705);
            Text(w,320,105,.4f,h.bossClock%360>=100&&h.bossClock%360<275?"SHOOT THE EXPOSED HEAD / GRENADE TO INTERRUPT":"GUARD UP - WAIT FOR THE OPENING");
        }
    } else if(h.phase==Clear || h.phase==Defeat) {
        Text(w,320,155,.85f,h.phase==Clear?"CHAPTER COMPLETE":"MISSION FAILED",h.phase==Clear?gold:red);
        Text(w,320,220,.55f,"ENEMIES DEFEATED  "+std::to_string(h.kills));
        Text(w,320,250,.55f,"ACCURACY  "+std::to_string(h.shots?h.hits*100/h.shots:0)+" PERCENT");
        Text(w,320,285,.48f,h.route==1?"ROUTE - DOWNSTAIRS":"ROUTE - UPSTAIRS",blue);
        if(h.tick>=180) Text(w,320,335,.5f,"CLICK OR ENTER - RETURN TO MENU",gold);
    }
    if(h.phase==Fight && (h.encounter==2||h.encounter==8) && h.message!="SUPPLY COLLECTED") Text(w,74,185,.36f,"SUPPLY +",gold);
    if(h.blast>0) {
        Mat4 blast=Mat4::Identity(); mx::Translate(blast,0,0,-30); mx::Scale(blast,2.8f,2.8f,2.8f);
        w.Draw(0x125+24-h.blast,blast,0xE,.8f,false,-1,true);
        --h.blast;
    }
    for(int p=0;p<2;++p) if(w.player[p].state==5) {
        Text(w,p?505:135,402,.47f,"SMG "+std::to_string(w.player[p].ammo)+" OF 30",gold);
        Text(w,p?505:135,425,.42f,"GRENADES "+std::to_string(h.grenades[p]),blue);
        Text(w,p?505:135,449,.42f,"LIVES "+std::to_string(w.player[p].lives)+"   SCORE "+std::to_string(w.player[p].score));
        if(h.reload[p]) Text(w,p?505:135,375,.5f,"RELOADING",gold);
        if(h.invincible[p]>65) Text(w,p?505:135,345,.7f,"HIT",red);
    }
}
}

bool PrototypeActive(const World::Impl& w) { return w.prototype!=nullptr && w.gameMode==6; }
bool PrototypeTryStart(World::Impl& w) {
    if(!w.cfg.portFeatures || w.cfg.portNetworkRoom || (w.gameMode!=3 && w.gameMode!=4) || !(w.pressed&kPrototypeKey)) return false;
    WorldConfig cfg=w.cfg; cfg.gameMode=6; cfg.gameType=0; cfg.eventMode=0; cfg.scene=0; cfg.startFrame=w.frame;
    cfg.playerState[0]=0; cfg.playerState[1]=9; cfg.carry.valid=false;
    w.front.gameType=0; w.front.sceneSelect=false; w.front.hordeStart=false; w.front.randomizerStart=false;
    RandomizerBegin(w,false); w.FrontStartStage(cfg); w.ts.Reset();
    w.prototype=std::make_shared<State>(); w.residency=false; w.resQueue.clear();
    for(auto& slot:w.actorSlots) slot=nullptr;
    w.targets.clear(); w.targetCopy.clear(); w.front.screens.clear(); w.hud.screens.clear();
    w.flags5900=0; w.subState=2; w.paused=false; w.letterbox=0; w.canShoot=1; w.padClear=0; w.demoFlag=0;
    w.player[0].state=5; w.player[0].lives=w.player[0].livesShown=5; w.player[0].ammo=30;
    w.player[1].state=9; w.players=1; w.playersInGame=0;
    w.SoundStopAll(); w.Sound(0x10000013); w.Sound(0x121A9);
    PrototypeFrame(w); return true;
}
void PrototypeFrame(World::Impl& w) {
    State& h=S(w); ++h.tick; ++h.age;
    if(h.phase==Intro && (h.tick>=360 || (h.tick>=180 && (w.pressed&0xC)))) StartFight(w,h);
    if(h.phase==Travel) {
        Point dest=Eye(h.encounter),a=h.from,b=dest;
        const bool cut=h.from.x!=dest.x || h.encounter==9;
        const int duration=cut?120:180;
        float t=std::min(1.f,h.tick/180.f);
        const float turn=std::min(std::clamp((t-.43f)/.14f,0.f,1.f),std::clamp((.97f-t)/.14f,0.f,1.f));
        h.yaw=h.encounter==9?0:static_cast<int>(-std::atan2(dest.x-h.from.x,-(dest.z+37-(h.from.z-138)))*mx::kToAngle*turn);
        if(h.encounter!=9) {
            const Point exit{h.from.x,h.from.y,h.from.z-138}, entry{dest.x,dest.y,dest.z+37};
            if(t<.55f) { b=exit; t/=.55f; }
            else if(t<.85f) { a=exit; b=entry; t=(t-.55f)/.3f; }
            else { a=entry; t=(t-.85f)/.15f; }
        }
        const float smooth=t*t*(3-2*t);
        h.camera={a.x+(b.x-a.x)*smooth,a.y+(b.y-a.y)*smooth,a.z+(b.z-a.z)*smooth};
        if(cut) { h.camera=h.tick<60?h.from:dest; h.yaw=0; }
        if(h.tick>=duration) { h.camera=dest; h.message.clear(); StartFight(w,h); }
    }
    if(h.phase==Fork && (h.tick>=600 || (w.pressed&0xC0))) {
        h.route=(w.pressed&0x80)?1:0; h.encounter=h.route==0?4:6; h.from=h.camera; Change(h,Travel);
    }
    if(h.phase==Grab) {
        if(h.escape>=18 || h.tick>=240) {
            if(h.escape<18) Hurt(w,h,w.player[0].state==5?0:1);
            if(h.phase!=Defeat) Change(h,static_cast<Phase>(h.returnPhase));
        }
    }
    if(h.phase!=Travel) h.yaw=0;
    Camera(w,h); DrawEnvironment(w,h);
    if(h.phase==Fight || h.phase==Boss) Combat(w,h);
    DrawBodies(w,h,true); Controls(w,h); Hud(w,h);
    w.liveEnemies=0; for(const auto& b:h.bodies) if(!b.death) ++w.liveEnemies;
    w.canShoot=h.phase==Fight||h.phase==Boss||h.phase==Fork||h.phase==Grab;
    if((h.phase==Clear || h.phase==Defeat) && h.tick>=180 && (w.pressed&0xC000C)) w.front.modeRequest=4;
    if(h.tick==1) w.Note("prototype: encounter "+std::to_string(h.encounter)+" phase "+std::to_string(h.phase));
}
PrototypeStatus PrototypeInspect(const World::Impl& w) {
    PrototypeStatus out; if(!PrototypeActive(w)) return out; const auto& h=S(w);
    out.encounter=h.encounter; out.route=h.route; out.phase=h.phase; out.age=h.age; out.kills=h.kills;
    out.grenades=h.grenades[0]; out.complete=h.phase==Clear;
    for(const auto& b:h.bodies) if(!b.death) out.targets.push_back({b.headX,b.headY,float(b.hp)});
    return out;
}
}
