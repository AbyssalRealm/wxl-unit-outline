// wxl-unit-outline: a reaction-colored silhouette outline on the mouseover and target units.
// A render script: it owns its shaders and logic and draws through the core gx facade; it never
// touches an offset or installs a hook. It subscribes to render events and self-registers at load.
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

namespace
{
    // Writes the model color into the mask, following the texture alpha cutout (wings, etc.).
    const char* kColorHLSL =
        "sampler2D s0 : register(s0);\n"
        "float4 c0 : register(c0);\n"
        "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
        "  clip(tex2D(s0, uv).a - 0.5);\n"
        "  return c0;\n"
        "}\n";

    // 8-tap edge detect on the mask: a thin anti-aliased line outside the silhouette.
    const char* kEdgeHLSL =
        "sampler2D m : register(s0);\n"
        "float4 px : register(c0);\n"
        "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
        "  float2 o = px.xy * px.z;\n"
        "  float4 a = tex2D(m,uv+float2(o.x,0)) + tex2D(m,uv+float2(-o.x,0))\n"
        "           + tex2D(m,uv+float2(0,o.y)) + tex2D(m,uv+float2(0,-o.y))\n"
        "           + tex2D(m,uv+float2(o.x,o.y)) + tex2D(m,uv+float2(-o.x,-o.y))\n"
        "           + tex2D(m,uv+float2(o.x,-o.y)) + tex2D(m,uv+float2(-o.x,o.y));\n"
        "  float inside = tex2D(m, uv).a;\n"
        "  float outline = saturate(a.a * 0.125 * 1.6) * (1.0 - inside);\n"
        "  clip(outline - 0.02);\n"
        "  float3 col = a.a > 0.001 ? a.rgb / a.a : float3(1,1,1);\n"
        "  return float4(col, saturate(outline * 1.4));\n"
        "}\n";
}