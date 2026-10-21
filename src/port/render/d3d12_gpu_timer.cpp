#include "d3d12_gpu_timer.h"
#ifdef _WIN32
#include <cmath>
#include <cstring>
namespace port {
bool D3D12GpuTimer::Initialize(ID3D12Device* device,ID3D12CommandQueue* queue){
    device_=device;frequency_=0;return device&&queue&&SUCCEEDED(queue->GetTimestampFrequency(&frequency_))&&frequency_>0;
}
bool D3D12GpuTimer::Enable(bool enabled,std::string* error){
    if(enabled&&!frequency_){if(error)*error="Native D3D12 queue timestamps are unavailable";return false;}
    if(enabled&&!queries_){
        D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=2;
        if(FAILED(device_->CreateQueryHeap(&query,IID_PPV_ARGS(&queries_)))){if(error)*error="Native D3D12 timestamp query allocation failed";return false;}
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=16;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_)))){queries_.Reset();if(error)*error="Native D3D12 timestamp readback allocation failed";return false;}
    }
    enabled_=enabled;active_=pending_=fresh_=false;return true;
}
void D3D12GpuTimer::Begin(ID3D12GraphicsCommandList* commands,uint64_t serial,int width,int height){
    active_=enabled_;split_=false;if(!active_)return;current_={serial,width,height,0,false};commands->EndQuery(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
}
void D3D12GpuTimer::Split(){if(active_)split_=true;}
void D3D12GpuTimer::End(ID3D12GraphicsCommandList* commands){
    if(!active_)return;active_=false;if(split_)return;
    commands->EndQuery(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);commands->ResolveQueryData(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,readback_.Get(),0);pending_=true;
}
void D3D12GpuTimer::Complete(){
    if(!pending_)return;pending_=false;void* mapped=nullptr;const D3D12_RANGE range{0,16};if(FAILED(readback_->Map(0,&range,&mapped)))return;
    uint64_t ticks[2]{};memcpy(ticks,mapped,sizeof(ticks));D3D12_RANGE empty{};readback_->Unmap(0,&empty);
    if(ticks[1]<=ticks[0])return;current_.milliseconds=double(ticks[1]-ticks[0])*1000/double(frequency_);current_.valid=std::isfinite(current_.milliseconds)&&current_.milliseconds>0;
    if(current_.valid){latest_=current_;fresh_=true;}
}
bool D3D12GpuTimer::Poll(GpuTimingSample* sample){if(!sample||!fresh_)return false;*sample=latest_;fresh_=false;return true;}
}
#endif
