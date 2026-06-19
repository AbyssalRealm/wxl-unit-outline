// wxl-unit-outline: reaction-colored silhouette outline on the mouseover and target units.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "Outline.hpp"
#include "Hlsl.hpp"

#include "game/unit/Unit.hpp"
#include "game/world/World.hpp"

namespace wxl::scripts::outline
{
    namespace gx    = wxl::game::gx;
    namespace ev    = wxl::events;
    namespace world = wxl::game::world;
    namespace unit  = wxl::game::unit;

    constexpr uint32_t kFmtA8R8G8B8 = 21;

    Outline::Outline()
    {
        on<&Outline::OnEndScene>(ev::Event::OnEndScene);
        on<&Outline::OnM2Batch>(ev::Event::OnM2BatchDraw);
    }

    void Outline::ColorForReaction(int reaction, float* c)
    {
        if (reaction < 2)      { c[0] = 1.0f; c[1] = 0.0f; c[2] = 0.0f; } // hostile
        else if (reaction < 4) { c[0] = 1.0f; c[1] = 1.0f; c[2] = 0.0f; } // neutral
        else                   { c[0] = 0.0f; c[1] = 1.0f; c[2] = 0.0f; } // friendly
        c[3] = 1.0f;
    }

    bool Outline::EnsureResources(gx::Device9 dev)
    {
        if (!colorPS_)      colorPS_ = gx::CompilePixelShader(dev, kColorHLSL, "ps_2_0");
        if (!edgePS_)       edgePS_  = gx::CompilePixelShader(dev, kEdgeHLSL,  "ps_2_0");
        if (!mask_.surface) gx::EnsureBackbufferTarget(dev, mask_, kFmtA8R8G8B8);
        return colorPS_ && edgePS_ && mask_.surface;
    }

    int Outline::FindTarget(void* model) const
    {
        for (int hop = 0; model && hop < 8; ++hop, model = unit::ModelParent(model))
            for (int i = 0; i < count_; ++i)
                if (targets_[i].model == model) return i;
        return -1;
    }

    void Outline::AddTarget(unsigned long long guid, void* player)
    {
        if (!guid || count_ >= kMaxTargets) return;
        const bool isPlayer = (guid >> 32) == 0;

        void* obj = world::GetObject(guid, isPlayer ? world::kTypeMaskPlayer : world::kTypeMaskUnit);
        if (!obj) return;

        void* model = unit::Model(obj);
        if (!model) return;

        for (int i = 0; i < count_; ++i)
            if (targets_[i].model == model) return; // dedup

        const int reaction = player ? unit::Reaction(obj, player) : 5;
        targets_[count_].model    = model;
        targets_[count_].isPlayer = isPlayer;
        ColorForReaction(reaction, targets_[count_].color);
        ++count_;
    }

    void Outline::RebuildTargets()
    {
        void* player = world::GetObject(world::ActivePlayerGuid(), world::kTypeMaskPlayer);
        count_ = 0;
        AddTarget(world::MouseoverGuid(), player);
        AddTarget(world::TargetGuid(),    player);
    }

    // Draw the model again into the mask render target, with full device-state save/restore.
    void Outline::StampSilhouette(gx::Device9 dev, const ev::M2BatchDrawArgs& a, int idx)
    {
        void* oldRT = nullptr; dev.GetRenderTarget(0, &oldRT);
        void* oldDS = nullptr; dev.GetDepthStencil(&oldDS);
        void* oldPS = nullptr; dev.GetPixelShader(&oldPS);
        unsigned char oldVP[24]; dev.GetViewport(oldVP);
        const unsigned sAB = dev.GetRenderState(gx::rs::kAlphaBlend);
        const unsigned sZE = dev.GetRenderState(gx::rs::kZEnable);
        const unsigned sZW = dev.GetRenderState(gx::rs::kZWrite);
        const unsigned sZF = dev.GetRenderState(gx::rs::kZFunc);

        dev.SetRenderTarget(0, mask_.surface);
        if (targets_[idx].isPlayer)
        {
            // Players: depth-test against the scene so walls occlude the outline.
            dev.SetDepthStencil(oldDS);
            dev.SetRenderState(gx::rs::kZEnable, 1);
            dev.SetRenderState(gx::rs::kZWrite, 0);
            dev.SetRenderState(gx::rs::kZFunc, gx::cmp::kLessEqual);
        }
        else
        {
            // NPCs: see-through (no depth).
            dev.SetDepthStencil(nullptr);
            dev.SetRenderState(gx::rs::kZEnable, 0);
        }
        if (!maskCleared_)
        {
            dev.Clear(0, nullptr, gx::clear::kTarget, 0x00000000, 1.0f, 0);
            maskCleared_ = true;
        }
        dev.SetRenderState(gx::rs::kAlphaBlend, 0);
        dev.SetPixelShader(colorPS_);
        dev.SetPixelShaderConstantF(0, targets_[idx].color, 1);
        dev.DrawIndexedPrimitive(a.primType, a.baseVertex, a.minIndex, a.numVerts, a.startIndex, a.primCount);

        dev.SetPixelShader(oldPS);
        dev.SetRenderTarget(0, oldRT);
        dev.SetDepthStencil(oldDS);
        dev.SetViewport(oldVP);
        dev.SetRenderState(gx::rs::kAlphaBlend, sAB);
        dev.SetRenderState(gx::rs::kZEnable, sZE);
        dev.SetRenderState(gx::rs::kZWrite, sZW);
        dev.SetRenderState(gx::rs::kZFunc, sZF);
        gx::Release(oldRT);
        gx::Release(oldDS);
        gx::Release(oldPS);
    }

    void Outline::EdgePass(gx::Device9 dev)
    {
        const unsigned sZE = dev.GetRenderState(gx::rs::kZEnable);
        const unsigned sAB = dev.GetRenderState(gx::rs::kAlphaBlend);
        const unsigned sSB = dev.GetRenderState(gx::rs::kSrcBlend);
        const unsigned sDB = dev.GetRenderState(gx::rs::kDestBlend);
        const unsigned sCU = dev.GetRenderState(gx::rs::kCullMode);

        dev.SetRenderState(gx::rs::kZEnable, 0);
        dev.SetRenderState(gx::rs::kCullMode, gx::cull::kNone);
        dev.SetRenderState(gx::rs::kAlphaBlend, 1);
        dev.SetRenderState(gx::rs::kSrcBlend, gx::blend::kSrcAlpha);
        dev.SetRenderState(gx::rs::kDestBlend, gx::blend::kInvSrcAlpha);
        dev.SetVertexShader(nullptr);
        dev.SetTexture(0, mask_.texture);
        dev.SetPixelShader(edgePS_);

        const float c0[4] = { 1.0f / mask_.width, 1.0f / mask_.height, thickness_, 0.0f };
        dev.SetPixelShaderConstantF(0, c0, 1);
        gx::DrawFullscreenQuad(dev);

        dev.SetPixelShader(nullptr);
        dev.SetTexture(0, nullptr);
        dev.SetRenderState(gx::rs::kZEnable, sZE);
        dev.SetRenderState(gx::rs::kCullMode, sCU);
        dev.SetRenderState(gx::rs::kAlphaBlend, sAB);
        dev.SetRenderState(gx::rs::kSrcBlend, sSB);
        dev.SetRenderState(gx::rs::kDestBlend, sDB);
    }

    void Outline::OnM2Batch(const ev::M2BatchDrawArgs& a)
    {
        if (count_ == 0 || !colorPS_ || !mask_.surface) return;
        const int idx = FindTarget(a.model);
        if (idx < 0) return;
        StampSilhouette(gx::Device9(a.device), a, idx);
    }

    void Outline::OnEndScene(const ev::EndSceneArgs& a)
    {
        gx::Device9 dev(a.device);
        if (!dev) return;

        if (EnsureResources(dev) && maskCleared_)
            EdgePass(dev);

        RebuildTargets();
        maskCleared_ = false;
    }

    // Self-registration: the file-scope instance binds its handlers at DLL load via the EventScript ctor.
    Outline g_outline;
}
