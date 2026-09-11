#pragma once
#include "geometry.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace nemesis::preview3d {
template<class T> using com_ptr=Microsoft::WRL::ComPtr<T>;
inline void check_hr(HRESULT hr,const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(std::string(operation)+" failed, HRESULT="+std::to_string(static_cast<unsigned long>(hr)));
}
class renderer {
    com_ptr<ID3D11Device> device_;
    com_ptr<ID3D11DeviceContext> deferred_;
    com_ptr<ID3D11VertexShader> vs_;
    com_ptr<ID3D11PixelShader> ps_;
    com_ptr<ID3D11InputLayout> layout_;
    com_ptr<ID3D11Buffer> vertices_,constants_;
    com_ptr<ID3D11RasterizerState> raster_;
    com_ptr<ID3D11DepthStencilState> depth_state_;
    com_ptr<ID3D11SamplerState> sampler_;
    com_ptr<ID3D11ShaderResourceView> texture_,white_,view_;
    com_ptr<ID3D11Texture2D> target_,depth_;
    com_ptr<ID3D11RenderTargetView> rtv_;
    com_ptr<ID3D11DepthStencilView> dsv_;
    UINT width_{},height_{},count_{};
    struct constants {
        std::array<float,4> eye,right,up,forward,projection;
    };
    static_assert(sizeof(constants)==80);
    static constexpr char shader_[]=R"HLSL(
cbuffer Camera : register(b0) {
    float4 Eye; float4 Right; float4 Up; float4 Forward; float4 Projection;
};
Texture2D Albedo : register(t0);
SamplerState Linear : register(s0);
struct VSIn { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct VSOut { float4 position : SV_POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
VSOut vs_main(VSIn v) {
    VSOut o;
    float3 p=v.position-Eye.xyz;
    float z=dot(p,Forward.xyz);
    o.position=float4(dot(p,Right.xyz)*Projection.x,dot(p,Up.xyz)*Projection.y,
                      z*Projection.z+Projection.w,z);
    o.normal=v.normal;
    o.uv=float2(v.uv.x,1-v.uv.y);
    return o;
}
float4 ps_main(VSOut p, bool front : SV_IsFrontFace) : SV_TARGET {
    float3 n=normalize(p.normal);
    float key=abs(dot(n,normalize(float3(-0.5,0.8,-0.7))));
    float fill=abs(dot(n,normalize(float3(0.8,0.2,0.4))));
    float3 base=Albedo.Sample(Linear,p.uv).rgb;
    return float4(saturate(base*(0.24+0.62*key+0.14*fill)),1);
}
)HLSL";
    void resize(UINT width,UINT height) {
        if (width==width_ && height==height_ && view_) return;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        com_ptr<ID3D11Texture2D> target,depth;
        com_ptr<ID3D11RenderTargetView> rtv;
        com_ptr<ID3D11ShaderResourceView> view;
        com_ptr<ID3D11DepthStencilView> dsv;
        check_hr(device_->CreateTexture2D(&desc,nullptr,target.GetAddressOf()),"Create preview color target");
        check_hr(device_->CreateRenderTargetView(target.Get(),nullptr,rtv.GetAddressOf()),"Create preview RTV");
        check_hr(device_->CreateShaderResourceView(target.Get(),nullptr,view.GetAddressOf()),"Create preview SRV");
        desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        check_hr(device_->CreateTexture2D(&desc,nullptr,depth.GetAddressOf()),"Create preview depth target");
        check_hr(device_->CreateDepthStencilView(depth.Get(),nullptr,dsv.GetAddressOf()),"Create preview DSV");
        target_=std::move(target); depth_=std::move(depth); rtv_=std::move(rtv);
        view_=std::move(view); dsv_=std::move(dsv); width_=width; height_=height;
    }
public:
    explicit renderer(ID3D11Device* device):device_(device) {
        if (!device) throw std::runtime_error("D3D11 device is unavailable");
        check_hr(device->CreateDeferredContext(0,deferred_.GetAddressOf()),"Create deferred preview context");
        com_ptr<ID3DBlob> vs,ps,errors;
        auto compile=[&](const char* entry,const char* profile,com_ptr<ID3DBlob>& code) {
            errors.Reset();
            const auto hr=D3DCompile(shader_,sizeof(shader_)-1,"nemesis-local-preview",nullptr,nullptr,
                                    entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,code.GetAddressOf(),errors.GetAddressOf());
            if (FAILED(hr)) {
                const std::string details=errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()) : "no compiler diagnostics";
                throw std::runtime_error("Preview shader: "+details);
            }
        };
        compile("vs_main","vs_4_0",vs); compile("ps_main","ps_4_0",ps);
        check_hr(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vs_.GetAddressOf()),"Create vertex shader");
        check_hr(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,ps_.GetAddressOf()),"Create pixel shader");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}
        };
        check_hr(device->CreateInputLayout(elements,3,vs->GetBufferPointer(),vs->GetBufferSize(),layout_.GetAddressOf()),"Create input layout");
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth=sizeof(constants); cb.Usage=D3D11_USAGE_DYNAMIC;
        cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check_hr(device->CreateBuffer(&cb,nullptr,constants_.GetAddressOf()),"Create camera constants");
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode=D3D11_FILL_SOLID; rs.CullMode=D3D11_CULL_NONE; rs.DepthClipEnable=TRUE;
        check_hr(device->CreateRasterizerState(&rs,raster_.GetAddressOf()),"Create rasterizer state");
        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable=TRUE; ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        check_hr(device->CreateDepthStencilState(&ds,depth_state_.GetAddressOf()),"Create depth state");
        D3D11_SAMPLER_DESC ss{};
        ss.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        ss.ComparisonFunc=D3D11_COMPARISON_ALWAYS; ss.MaxLOD=D3D11_FLOAT32_MAX;
        check_hr(device->CreateSamplerState(&ss,sampler_.GetAddressOf()),"Create texture sampler");
        D3D11_TEXTURE2D_DESC td{};
        td.Width=td.Height=td.MipLevels=td.ArraySize=1; td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const std::uint32_t pixel=0xffc5cbd1u;
        const D3D11_SUBRESOURCE_DATA init{&pixel,4,0};
        com_ptr<ID3D11Texture2D> tex;
        check_hr(device->CreateTexture2D(&td,&init,tex.GetAddressOf()),"Create neutral material");
        check_hr(device->CreateShaderResourceView(tex.Get(),nullptr,white_.GetAddressOf()),"Create neutral SRV");
    }
    void upload(const mesh& m, ID3D11ShaderResourceView* texture) {
        if (m.vertices.empty()) throw std::runtime_error("No vertices to upload");
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=static_cast<UINT>(m.vertices.size()*sizeof(vertex));
        desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        const D3D11_SUBRESOURCE_DATA init{m.vertices.data(),0,0};
        com_ptr<ID3D11Buffer> buffer;
        check_hr(device_->CreateBuffer(&desc,&init,buffer.GetAddressOf()),"Upload OBJ vertices");
        vertices_=std::move(buffer); count_=static_cast<UINT>(m.vertices.size()); texture_=texture;
    }
    ID3D11Device* device() const { return device_.Get(); }
    ID3D11ShaderResourceView* render(const camera& cam, UINT width, UINT height) {
        if (!vertices_) return nullptr;
        resize(std::clamp(width,UINT{16},UINT{4096}),std::clamp(height,UINT{16},UINT{4096}));
        // Only the deferred context is modified. RestoreContextState=TRUE keeps
        // the host's immediate-context pipeline unchanged after execution.
        deferred_->ClearState();
        const auto e=cam.eye(),r=cam.right(),u=cam.up(),f=cam.forward();
        constexpr float near_z=0.02f,far_z=100.0f;
        const constants data{{e.x,e.y,e.z,0},{r.x,r.y,r.z,0},{u.x,u.y,u.z,0},{f.x,f.y,f.z,0},
            {1.0f/(camera::tangent*static_cast<float>(width_)/height_),1.0f/camera::tangent,far_z/(far_z-near_z),-near_z*far_z/(far_z-near_z)}};
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check_hr(deferred_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Map camera constants");
        std::memcpy(mapped.pData,&data,sizeof(data)); deferred_->Unmap(constants_.Get(),0);
        ID3D11RenderTargetView* rtv=rtv_.Get();
        deferred_->OMSetRenderTargets(1,&rtv,dsv_.Get());
        const float clear[]={0.035f,0.045f,0.06f,1.0f};
        deferred_->ClearRenderTargetView(rtv,clear);
        deferred_->ClearDepthStencilView(dsv_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1.0f,0);
        const D3D11_VIEWPORT vp{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        deferred_->RSSetViewports(1,&vp); deferred_->RSSetState(raster_.Get());
        deferred_->OMSetDepthStencilState(depth_state_.Get(),0);
        deferred_->OMSetBlendState(nullptr,nullptr,0xffffffffu);
        deferred_->IASetInputLayout(layout_.Get());
        deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const UINT stride=sizeof(vertex),offset=0;
        ID3D11Buffer* vb=vertices_.Get(); ID3D11Buffer* cb=constants_.Get();
        deferred_->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        deferred_->VSSetShader(vs_.Get(),nullptr,0); deferred_->VSSetConstantBuffers(0,1,&cb);
        deferred_->PSSetShader(ps_.Get(),nullptr,0);
        ID3D11ShaderResourceView* srv=texture_ ? texture_.Get() : white_.Get();
        ID3D11SamplerState* sample=sampler_.Get();
        deferred_->PSSetShaderResources(0,1,&srv); deferred_->PSSetSamplers(0,1,&sample);
        deferred_->Draw(count_,0);
        com_ptr<ID3D11CommandList> commands;
        check_hr(deferred_->FinishCommandList(FALSE,commands.GetAddressOf()),"Finish preview command list");
        com_ptr<ID3D11DeviceContext> immediate;
        device_->GetImmediateContext(immediate.GetAddressOf());
        immediate->ExecuteCommandList(commands.Get(),TRUE);
        return view_.Get();
    }
};
}
