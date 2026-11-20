#!/usr/bin/env python3
"""Translate the retained shader expressions to MSL; no retail EXE needed.
Only the already generated expressions are rewritten. Unknown layout or
constructs fail the build instead of inventing an instruction translation.
"""
import re
import sys
from pathlib import Path
from generate_metal_vertices import vertex_source

root = Path(__file__).resolve().parents[2]
header = r'''#include <metal_stdlib>
#pragma clang fp contract(off)
using namespace metal;
constant int4 pc_program0 [[function_constant(0)]];
constant int4 pc_program1 [[function_constant(1)]];
constant int4 pc_program2 [[function_constant(2)]];
constant int4 pc_program3 [[function_constant(3)]];
struct Input { packed_float4 p,d0,d1,t0,t1,t2,t3,t4,t5,t6,t7; float fog; };
struct Varying { float4 p [[position]]; float4 d0,d1,t0,t1,t2,t3,t4,t5,t6,t7; float fog; };
struct Uniforms {
 float4 PC[8]; int4 program[4],info,stage_kind[2]; float4 bump[8],lum[2];
 int4 op[4],arg[4],misc[4]; float4 konst[4],tfactor; int4 flags;
 float4 fog_colour,fog_params,alpha_test; float lod_bias[6];
};
struct IO {
 array<texture2d<float>,6> tex2d;
 array<texturecube<float>,6> texcube;
 array<sampler,6> smp;
};
// Stereo (port enhancement, MetalD3D9Device::set_stereo): eye 0 / 1 of a
// layered back-buffer pass moves by s.x / s.y * (w - s.z); 0 when mono.
// Screen-space positions (w == 1) move by s.x / s.y * s.w: 0 for the 2D
// layer, or the NDC shift of a 3D point the draw is placed over (the HUD's
// markers over rival cars, set_stereo_screen_depth). Nearer than the
// convergence depth, the (crossed) parallax is capped at the far one's.
static float4 pc_stereo(float4 p,ushort eye,float4 s){ const float e=eye?s.y:s.x; if(fabs(p.w-1.0f)>1.0e-5f)p.x+=e*max(p.w-s.z,-p.w); else p.x+=e*s.w; return p; }
vertex Varying pc_vertex(uint i [[vertex_id]],ushort eye [[amplification_id]],const device Input* v [[buffer(0)]],constant float4& stereo [[buffer(7)]]) {
 Varying o; o.p=pc_stereo(v[i].p,eye,stereo); o.d0=v[i].d0;o.d1=v[i].d1;
 o.t0=v[i].t0;o.t1=v[i].t1;o.t2=v[i].t2;o.t3=v[i].t3;
 o.t4=v[i].t4;o.t5=v[i].t5;o.t6=v[i].t6;o.t7=v[i].t7;o.fog=v[i].fog;return o;
}
float4 greaterThan(float4 a,float4 b){return select(float4(0),float4(1),a>b);}
// Fixed-function state as function constants (port optimisation, MetalD3D9Device::flush):
// ffp_fixed pipelines take the stage cascade (op / arg / misc of stages 0..3) and
// the epilogue switches (specular, alpha test enable and function, fog mode) from
// ffp_state, so the compiler folds the switches; the generic pipeline
// (ffp_fixed false) reads them from the uniforms. Same arithmetic either way.
constant bool ffp_fixed [[function_constant(32)]];
constant int4 ffp_state0 [[function_constant(33)]];constant int4 ffp_state1 [[function_constant(34)]];
constant int4 ffp_state2 [[function_constant(35)]];constant int4 ffp_state3 [[function_constant(36)]];
constant int4 ffp_state4 [[function_constant(37)]];constant int4 ffp_state5 [[function_constant(38)]];
constant int4 ffp_state6 [[function_constant(39)]];constant int4 ffp_state7 [[function_constant(40)]];
constant int4 ffp_state8 [[function_constant(41)]];constant int4 ffp_state9 [[function_constant(42)]];
constant int4 ffp_state10 [[function_constant(43)]];constant int4 ffp_state11 [[function_constant(44)]];
constant int4 ffp_state12 [[function_constant(45)]];
static int4 ffp_pick(int s,int4 a,int4 b,int4 c,int4 d){return s==0?a:s==1?b:s==2?c:d;}
'''
fields = 'PC program info stage_kind bump lum op arg misc konst tfactor flags fog_colour fog_params alpha_test'.split()
parts = [header, vertex_source(root)]
for name, file in [('ffp','pc_ffp_fsh.glsl'),('ps11','pc_ps11_uber_fsh.glsl'),('ps14','pc_ps14_uber_fsh.glsl')]:
    source = (root/'switch/shaders'/file).read_text()   # the PS5 copies are identical
    source = re.sub(r'^layout\([^\n]+\n', '', source, flags=re.M)
    source = re.sub(r'^#version[^\n]+\n', '', source, flags=re.M)
    source = source.replace('void main()', 'float4 evaluate()')
    source = re.sub(r'out_colour=([^;]+);return;', r'return \1;', source)
    source = source.replace('out_colour=c;', 'return clamp(c,0.0f,1.0f);')
    source = source.replace('discard;', 'discard_fragment();')
    source = source.replace('gl_FragCoord', 'v.p')
    for field in fields:
        source = re.sub(r'\b'+field+r'\b', 'u.'+field, source)
    source = re.sub(r'u\.program\[([0-3])\]', r'pc_program\1', source)
    source = re.sub(r'\bin_([dt][0-7]|fog)\b', r'v.\1', source)
    for typ,metal in [('ivec4','int4'),('vec4','float4'),('vec3','float3'),('vec2','float2')]:
        source = re.sub(r'\b'+typ+r'\b', metal, source)
    source = source.replace('uintBitsToFloat(', 'as_type<float>(').replace('inversesqrt(', 'rsqrt(')
    # Texture resources are dynamic, but stage indices remain authored constants.
    source = re.sub(r'texture\((tex2d|texcube)(\d),([^;]+?)\)', lambda m:f'io.{m[1]}[{m[2]}].sample(io.smp[{m[2]}],{m[3]},bias(u.lod_bias[{m[2]}]))', source)
    source = re.sub(r'texture\((tex|cube)(\d),([^;]+?)\)', lambda m:f'io.{"tex2d" if m[1]=="tex" else "texcube"}[{m[2]}].sample(io.smp[{m[2]}],{m[3]},bias(u.lod_bias[{m[2]}]))', source)
    if name == 'ffp':
        # Stage and epilogue state through the function constants above.
        source = re.sub(r'u\.(op|arg|misc)\[([^\]]+)\]', r'ffp_\1(\2)', source)
        for old, new in [('u.flags.x!=0', '(ffp_fixed?ffp_state12.x:u.flags.x)!=0'),
                         ('u.alpha_test.x!=0.0', '(ffp_fixed?float(ffp_state12.y):u.alpha_test.x)!=0.0'),
                         ('int(u.alpha_test.z)', '(ffp_fixed?ffp_state12.z:int(u.alpha_test.z))'),
                         ('u.fog_params.x!=0.0', '(ffp_fixed?float(ffp_state12.w):u.fog_params.x)!=0.0'),
                         ('int(u.fog_params.x)', '(ffp_fixed?ffp_state12.w:int(u.fog_params.x))'),
                         ('    for(int s=0;s<4;++s){', '    _Pragma("clang loop unroll(full)")\n    for(int s=0;s<4;++s){')]:
            if source.count(old) != 1:
                raise ValueError(f'fixed-function specialisation: {old!r} found {source.count(old)} times')
            source = source.replace(old, new)
        if re.search(r'u\.(op|arg|misc)\[', re.sub(r'//[^\n]*', '', source)):
            raise ValueError('fixed-function stage state read outside the function constants')
        source = '''int4 ffp_op(int s){return ffp_fixed?ffp_pick(s,ffp_state0,ffp_state1,ffp_state2,ffp_state3):u.op[s];}
int4 ffp_arg(int s){return ffp_fixed?ffp_pick(s,ffp_state4,ffp_state5,ffp_state6,ffp_state7):u.arg[s];}
int4 ffp_misc(int s){return ffp_fixed?ffp_pick(s,ffp_state8,ffp_state9,ffp_state10,ffp_state11):u.misc[s];}
''' + source
    if 'layout(' in source or 'texture(' in source or 'out_colour' in source or 'gl_' in source:
        raise ValueError(f'untranslated shader construct in {file}')
    parts.append(f'struct Eval_{name} {{\n constant Uniforms& u; Varying v; IO io;\n'+source+'\n};\n')
    parts.append(f'''fragment float4 pc_{name}(Varying v [[stage_in]],constant Uniforms& u [[buffer(0)]],
 array<texture2d<float>,6> t [[texture(0)]],array<texturecube<float>,6> c [[texture(6)]],
 array<sampler,6> s [[sampler(0)]]) {{ Eval_{name} e{{u,v,{{t,c,s}}}};return e.evaluate(); }}\n''')
source='\n'.join(parts)
out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True)
out.write_text('#pragma once\nnamespace outrun::mac { inline constexpr const char* PcMetalSource=R"OR2MSL('+source+')OR2MSL"; }\n')
