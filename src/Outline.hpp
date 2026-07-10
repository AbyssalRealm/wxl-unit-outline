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

#pragma once

#include "events/Event.hpp"
#include "events/EventScript.hpp"
#include "game/gx/Gx.hpp"

// Reference render script. It owns its shaders, its target list and its render targets, and draws
// purely through the core gx facade and the unit/world bindings. It never touches an offset or installs
// a hook: it binds two member functions to render events and the core does the rest.
//
// Pipeline (one-frame): EndScene of frame N rebuilds the target list for frame N+1; during frame N+1 the
// per-batch event stamps each target's silhouette into a mask; EndScene of N+1 edge-detects the mask
// into the frame, then rebuilds again.
namespace wxl::scripts::outline
{
    class Outline final : public events::EventScript
    {
    public:
        Outline(); // binds the event handlers

    private:
        // --- event handlers ---
        void OnEndScene(const events::EndSceneArgs& a);
        void OnM2Batch(const events::M2BatchDrawArgs& a);

        // --- steps ---
        bool EnsureResources(game::gx::Device9 dev);  // compile shaders + create the mask RT, once
        void RebuildTargets();                        // mouseover + target -> colored entries
        void StampSilhouette(game::gx::Device9 dev, const events::M2BatchDrawArgs& a, int idx);
        void EdgePass(game::gx::Device9 dev);         // composite the mask into the frame

        // --- helpers ---
        bool ShouldStampBatch(game::gx::Device9 dev) const;
        int  FindTarget(void* model) const;           // model or any parent in the list
        void AddTarget(unsigned long long guid, void* player);
        static void ColorForReaction(int reaction, float* outRgba);

        static constexpr int kMaxTargets = 2;
        struct Target { void* model; float color[4]; bool isPlayer; };

        Target                     targets_[kMaxTargets]{};
        int                        count_       = 0;
        game::gx::RenderTarget     mask_{};
        void*                      colorPS_     = nullptr; // fills opaque batches into the silhouette mask
        void*                      cutoutPS_    = nullptr; // fills alpha-tested batches into the silhouette mask
        void*                      edgePS_      = nullptr; // edge-detects the mask into a line
        bool                       maskCleared_ = false;
        float                      thickness_   = 1.5f;
    };
}
