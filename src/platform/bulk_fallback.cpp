#include "platform/bulk_fallback.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace outrun::platform {
#if defined(OR2_NO_BULK)
// Built without bulk_tr.cpp (NOBULK=1): no function is translated in bulk.
const TranslatedFunction bulk_functions[]={{0,nullptr}};
const TranslatedCodeData bulk_code_data[]={{0,0,nullptr}};
#else
extern const TranslatedFunction bulk_functions[];
extern const TranslatedCodeData bulk_code_data[];
#endif
namespace {
// RET n of the functions a translated call can reach as a native service (stdcall / thiscall
// callees pop their arguments): tools/x86tr/pops_table.py.
const TranslatedPops bulk_pops[]={
#include "platform/bulk_pops.inc"
{0,0}};
// Services the bulk code reads back from ST0 (fstp right after the call): 4A6CF0, the car's
// distance along the course (the request manager's 4FC490 progress +7690 / +7694, whose
// per-frame difference scores "Don't crash!" (512760) and the other distance requests;
// the honk request's 509027), and 449940 (its result popped at 4FAF44).
const std::uint32_t bulk_st0_results[]={0x4a6cf0u,0x449940u,0u};
const TranslatedModule& module_without_state(){
    static const TranslatedModule m=[]{
        TranslatedModule t{bulk_functions,bulk_pops,{},nullptr,nullptr,nullptr,bulk_code_data};
        t.st0_results=bulk_st0_results;return t;}();
    return m;
}
}
BulkStats& bulk_stats(){static BulkStats s;return s;}
bool bulk_translated(std::uint32_t pc){return translated_has(module_without_state(),pc);}
std::uint32_t bulk_call(PcRaceMemory& m,const PcRaceService& service,const PcRaceCall& k,
                        driving::PcMatrixStack* matrices,std::uint32_t* crt_random,const TranslatedModule* next,
                        PcD3D9Device* device){
    TranslatedModule module=module_without_state();
    module.matrices=matrices;module.crt_random=crt_random;module.next=next;module.device=device;
    // A thread block for FS: (the SEH frames of the C++ code: mov eax,fs:[0] ... mov fs:[0],esp).
    static std::array<std::uint32_t,16> tib{0xffffffffu};
    constexpr std::uint32_t BulkTibBase=0x0efb8000u;
    module.fs_base=BulkTibBase;
    const auto tib_mark=m.mark();
    if(!m.mapped(BulkTibBase,4))m.map(BulkTibBase,reinterpret_cast<std::uint8_t*>(tib.data()),tib.size()*4u);
    struct Unmap{PcRaceMemory& m;std::size_t mark;~Unmap(){m.release(mark);}} unmap{m,tib_mark};
    auto& st=bulk_stats();
    if(st.calls[k.pc]==0u)std::fprintf(stderr,"[bulk] first call %06X (translated fallback: to port)\n",k.pc);   // once per address
    ++st.calls[k.pc];
    const std::vector<std::uint32_t> args(k.args.begin(),k.args.begin()+std::min<std::size_t>(k.argc,k.args.size()));
    try{return translated_call_list(m,service,module,k.pc,TranslatedRegisters{k.eax,k.ecx,0,0,0,0},args);}
    catch(...){
        if(std::getenv("OR2_BULK_DEBUG")&&st.faults[k.pc]==0u)std::fprintf(stderr,"[bulk] %06X faulted\n",k.pc);
        ++st.faults[k.pc];throw;}
}
}
