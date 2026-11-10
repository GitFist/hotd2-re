#pragma once

namespace port {
// Own SM5 implementation. Both stages use the same 24-byte structured node.
inline const char kNaomiGpuSortSharedHlsl[] = R"(
struct NativeNode {
    uint color;
    float depth;
    uint next;
    uint draw;
    uint primitive;
    uint blend;
};
cbuffer NativeSort : register(b2) {
    uint4 nativeSize; // width, height, node capacity, original draw ordinal
    uint4 nativeBlend; // source factor, destination factor
};
RWTexture2D<uint> nativeHeads : register(u1);
RWStructuredBuffer<NativeNode> nativeNodes : register(u2);
RWByteAddressBuffer nativeAllocator : register(u3);
static const uint NativeEnd = 0xffffffffu;
)";

inline const char kNaomiGpuGatherHlsl[] = R"(
[earlydepthstencil]
void psgather(VSOut i, uint primitive : SV_PrimitiveID) {
    float4 color = ModelColor(i);
    uint2 pixel = (uint2)i.pos.xy;
    if (any(pixel >= nativeSize.xy)) return;
    uint index;
    nativeAllocator.InterlockedAdd(0,1,index);
    if(index >= nativeSize.z) {
        uint ignored;
        nativeAllocator.InterlockedOr(4,1,ignored);
        return;
    }
    NativeNode node;
    // The blend target is RGBA8. Quantize once here instead of retaining four
    // floats through every sorting pass; this is the same conversion the
    // resolve used before evaluating the fixed-function blend factors.
    uint4 rgba=(uint4)round(saturate(color)*255.0);
    node.color=rgba.x|(rgba.y<<8)|(rgba.z<<16)|(rgba.w<<24);
    // The original TA sorts reciprocal eye depth, not the scaled D3D adapter.
    node.depth=1.0/i.depth;
    node.draw=nativeSize.w;node.primitive=primitive;
    node.blend=nativeBlend.x|(nativeBlend.y<<8);
    uint old;
    InterlockedExchange(nativeHeads[pixel],index,old);
    node.next=old;
    nativeNodes[index]=node;
}
)";

inline const char kNaomiGpuResolveHlsl[] = R"(
Texture2D<float4> nativeOpaque : register(t4);
float4 vsresolve(uint id : SV_VertexID) : SV_Position {
    float2 p=float2((id<<1)&2,id&2);
    return float4(p*float2(2,-2)+float2(-1,1),0,1);
}
bool NativeBeforeNodes(NativeNode x,NativeNode y,uint a,uint b) {
    if(x.depth != y.depth)return x.depth<y.depth;
    if(x.draw != y.draw)return x.draw<y.draw;
    if(x.primitive != y.primitive)return x.primitive<y.primitive;
    return a<b;
}
bool NativeBefore(uint a,uint b) { return NativeBeforeNodes(nativeNodes[a],nativeNodes[b],a,b); }
float4 NativeFactor(uint kind,float4 source,float4 destination) {
    if(kind==1)return 0;
    if(kind==3)return source;
    if(kind==4)return 1-source;
    if(kind==5)return source.aaaa;
    if(kind==6)return 1-source.aaaa;
    if(kind==7)return destination.aaaa;
    if(kind==8)return 1-destination.aaaa;
    if(kind==9)return destination;
    if(kind==10)return 1-destination;
    return 1;
}
float4 NativeBlend(NativeNode fragment,float4 result) {
    float4 source=float4(fragment.color&255,(fragment.color>>8)&255,
                         (fragment.color>>16)&255,fragment.color>>24)/255.0;
    float4 a=NativeFactor(fragment.blend&255,source,result);
    float4 b=NativeFactor((fragment.blend>>8)&255,source,result);
    return round(saturate(source*a+result*b)*255.0)/255.0;
}
float4 psresolve(float4 position : SV_Position) : SV_Target {
    uint2 pixel=(uint2)position.xy;
    uint head=nativeHeads[pixel];
    float4 result=nativeOpaque.Load(int3(pixel,0));
    // Most covered pixels have only one or two fragments. Resolve those
    // without rewriting a UAV linked list or running the merge-sort loop.
    [branch] if(head==NativeEnd)return result;
    NativeNode firstNode=nativeNodes[head];
    uint second=firstNode.next;
    [branch] if(second==NativeEnd)return NativeBlend(firstNode,result);
    NativeNode secondNode=nativeNodes[second];
    [branch] if(secondNode.next==NativeEnd) {
        if(!NativeBeforeNodes(firstNode,secondNode,head,second)) {
            NativeNode swap=firstNode;firstNode=secondNode;secondNode=swap;
        }
        return NativeBlend(secondNode,NativeBlend(firstNode,result));
    }
    // Bottom-up mergesort rewires this pixel's private list. No fixed local
    // array or per-pixel layer cap truncates the original fragments.
    uint run=1;
    [loop] while(head!=NativeEnd) {
        uint p=head,newHead=NativeEnd,tail=NativeEnd,merges=0;
        [loop] while(p!=NativeEnd) {
            ++merges;
            uint q=p,left=0;
            [loop] for(uint i=0;i<run&&q!=NativeEnd;++i) {++left;q=nativeNodes[q].next;}
            uint right=run;
            [loop] while(left>0 || (right>0&&q!=NativeEnd)) {
                uint item;
                if(left==0) {item=q;q=nativeNodes[q].next;--right;}
                else if(right==0||q==NativeEnd) {item=p;p=nativeNodes[p].next;--left;}
                else if(NativeBefore(p,q)) {item=p;p=nativeNodes[p].next;--left;}
                else {item=q;q=nativeNodes[q].next;--right;}
                if(tail==NativeEnd)newHead=item;else nativeNodes[tail].next=item;
                tail=item;
            }
            p=q;
        }
        if(tail!=NativeEnd)nativeNodes[tail].next=NativeEnd;
        head=newHead;
        if(merges<=1)break;
        run*=2;
    }
    [loop] for(uint node=head;node!=NativeEnd;node=nativeNodes[node].next) {
        // Match the RGBA8 target between every fixed-function blend, including
        // alpha's source/destination-color to alpha-factor mapping.
        result=NativeBlend(nativeNodes[node],result);
    }
    return result;
}
)";
} // namespace port
