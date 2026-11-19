#pragma once

// This game's D3D and XDK library functions, by name (tools/find_d3d.py; the hooks and the
// runtime use only these names). Generated: check the report before trusting a line.
#include "ppc_recomp_shared.h"

#define XD3D_PASTE2(a, b) a##b
#define XD3D_PASTE(a, b) XD3D_PASTE2(a, b)
#define XD3D_IMP(name) XD3D_PASTE(__imp__, XD3D_##name)

#define XD3D_BeginIndexedVertices sub_82AEF9C0  // by hand: DrawIndexedPrimitiveUP, as Gears' 8222D4F8: (device, primitive, base vertex, vertices, indices, index format, stride, &indices, &vertices on the stack +84); UE3 8287B270 / 8287B330
#define XD3D_BeginVertices sub_82AEF500  // by hand: DrawPrimitiveUP: (device, primitive, vertices, stride) returns the vertex pointer; UE3 8287B3E8 copies count * stride there and ends it inline (13716 -> 48)
#define XD3D_BindVertexShader sub_82AF9688  // by hand: IDirect3DVertexShader9::Bind without the flags: (shader, declaration, strides, pixel shader): reads the declaration's +0x1C and the shader's +896 vfetch table (+872), as Gears' 82234D70; UE3 (829643E0) binds each shader ahead, then draws with the declaration at 12120 = 0
#define XD3D_ClearRectangle sub_82AF16D8  // by hand: GTA 829D4360: (device, flags, ..., rect or a zero vector, ..., f1 depth); 0xC01C2B00 / 0xC00A2B00 packets, depth stencil 12704
#define XD3D_CreatePixelShader sub_82AF2660  // alike, 0.859: check it
#define XD3D_CreateVertexShader sub_82AF2A08  // alike, 1.0: check it
#define XD3D_DrawIndexedVertices sub_82AF0318  // by hand: DRAW_INDX (0xC0032201); UE3's RHIDrawIndexedPrimitive (8295EE78) calls SetIndices (82AECC08) then it, as Gears' 8254F0A0 calls 8222AFD8 then 8222DE50
#define XD3D_DrawVertices sub_82AEFF00  // by hand: DRAW_INDX auto-index (0xC0012201) after VGT_INDX_OFFSET = start vertex; UE3's RHIDrawPrimitive (8295EE10) calls it, as Gears' 827DBB10 calls 8222DA48
#define XD3D_GpuHangHandler sub_82B08120  // by hand: what GpuWaitCheck (82AF98D8) calls after 5000 ticks without progress, with the device
#define XD3D_GpuWaitCheck sub_82AF98D8  // by hand: one turn of D3D's GPU waits (state on the caller's stack: device, reason, read pointer, progress tick, start tick, start timebase +20, as Gears' 8222F460); called by the ring-space wait 82AF59B8 (in Swap), 82AF47F8, 82AF4E28, 82AF6B10, 82AFDFB0, 82AFEAB0
#define XD3D_KickOff sub_82AF5008  // by hand: the only writer of CP_RB_WPTR (0x7FC80000 + 1812), (device, ...)
#define XD3D_InitDisplay sub_82893550  // alike, 0.917: check it
#define XD3D_InitVertexShader sub_82AF2910  // called by CreateVertexShader
#define XD3D_LockResource sub_82AE8E40  // by hand: GTA 829D8F78: same arguments, andi. 4112, +12 / +8 by the flags
#define XD3D_LockTextureRect sub_82AEE798  // by hand: inlined: the worker both locks call (texture, array index, level, flags, ...); UE3 (8287BAF0, 8287D410, 82966208, 82966CE8) calls it itself, LockTextureRectArray (82AEE858) too (the hooks read it so: XD3D_XDK_20500)
#define XD3D_LockTextureRectArray sub_82AEE858  // by hand: GTA 829D2D18: (texture, array index, level, rect, box, flags) into the worker 82AEE798
#define XD3D_LzxCreateDecoder sub_82CF6DE8  // by calls, 0.909: check it
#define XD3D_LzxDecodeFrame sub_82CF6A30  // alike, 0.805: check it
#define XD3D_LzxDecodeInternal sub_82CF6210  // by calls, 0.954: check it
#define XD3D_LzxDestroyDecoder sub_82CDC828  // alike, 0.95: check it
#define XD3D_LzxResetDecoder sub_82CF69E8
#define XD3D_ReleaseResource sub_82AE93D0  // by hand: GTA 829D9338: lwarx add -1 on +4, releases the parent (+24) recursively
#define XD3D_Resolve sub_82AF34A8  // by hand: GTA 829CB818: (device, flags, rects, texture, point, level, slice, ..., f1 depth); render target 3172 / depth stencil 12704 for flags & 7
#define XD3D_SetDepthStencilSurface sub_82AED028  // alike, 0.948: check it
#define XD3D_SetDeviceBits10916 sub_82AFAA68  // alike, 0.929: check it
#define XD3D_SetIndices sub_82AECC08  // alike, 0.889: check it
#define XD3D_SetPixelShader sub_82AF2750  // alike, 0.917: check it
#define XD3D_SetPredication sub_82AFC928  // alike, 0.973: check it
#define XD3D_SetRenderTarget sub_82AECC98  // by hand: GTA 829CAB00: addi r11,r4,3172 (GTA 3108), the 0x20107 mask, 2593 / 3070 (GTA 3005)
#define XD3D_SetStreamSource sub_82AECAF0  // alike, 0.865: check it
#define XD3D_SetTexture sub_82AEF370  // by hand: GTA 829D3728: texture slot addi 3198 (GTA 3134), fetch constant (sampler + 48) * 24
#define XD3D_SetVertexShader sub_82AF2AF8  // alike, 0.948: check it
#define XD3D_Swap sub_82AF6B10  // by hand: GTA 829D5388: the only VdSwap caller; frame counter 16840 (GTA 16544), 24-byte copy from r4 + 28
#define XD3D_TilingRestoreState sub_82AEBE00  // alike, 0.905: check it
#define XD3D_UnlockResource sub_82AE9140  // by hand: GTA 829D91B0: lwarx add -256, lock count 0x100
// XD3D_AcquireCommandScratch: not found
// XD3D_BeginTiling: not found
// XD3D_CommandListBindFences: not found
// XD3D_CreateDevice: not found
// XD3D_DeleteCommandList: not found
// XD3D_DeviceClearFlagBit2: not found
// XD3D_DeviceCpuPrefix: not found
// XD3D_DirtyAllState: not found
// XD3D_EndTiling: not found
// XD3D_EndVertices: not found
// XD3D_GuestFree: not found
// XD3D_InitDeviceCpuState: not found
// XD3D_InitDisplayMode: not found
// XD3D_InitEngines: not found
// XD3D_InitRing: not found
// XD3D_LzxParseBlock: not found
// XD3D_PhysicalAlloc: not found
// XD3D_PresentWorker: not found
// XD3D_QueryFrameSlot: not found
// XD3D_QueryPerformanceFrequency: not found
// XD3D_RunCommandList: not found
// XD3D_SetPresentBehaviorBit: not found
// XD3D_SetScissorRect: not found
// XD3D_SetShaderGprAllocation: not found
// XD3D_SetVertexDeclaration: not found
// XD3D_SetVertexDeclarationDirect: not found
// XD3D_TilingRestoreConstants: not found

#define XD3D_ADDR_NullClearColor 0x8224A840
#define XD3D_ADDR_VdGlobalDevice 0x820009E4
// XD3D_ADDR_TilingRestoreArgument: not found

PPC_FUNC_IMPL(__imp__sub_82AEF9C0);
PPC_FUNC_IMPL(__imp__sub_82AEF500);
PPC_FUNC_IMPL(__imp__sub_82AF9688);
PPC_FUNC_IMPL(__imp__sub_82AF16D8);
PPC_FUNC_IMPL(__imp__sub_82AF2660);
PPC_FUNC_IMPL(__imp__sub_82AF2A08);
PPC_FUNC_IMPL(__imp__sub_82AF0318);
PPC_FUNC_IMPL(__imp__sub_82AEFF00);
PPC_FUNC_IMPL(__imp__sub_82B08120);
PPC_FUNC_IMPL(__imp__sub_82AF98D8);
PPC_FUNC_IMPL(__imp__sub_82AF5008);
PPC_FUNC_IMPL(__imp__sub_82893550);
PPC_FUNC_IMPL(__imp__sub_82AF2910);
PPC_FUNC_IMPL(__imp__sub_82AE8E40);
PPC_FUNC_IMPL(__imp__sub_82AEE798);
PPC_FUNC_IMPL(__imp__sub_82AEE858);
PPC_FUNC_IMPL(__imp__sub_82CF6DE8);
PPC_FUNC_IMPL(__imp__sub_82CF6A30);
PPC_FUNC_IMPL(__imp__sub_82CF6210);
PPC_FUNC_IMPL(__imp__sub_82CDC828);
PPC_FUNC_IMPL(__imp__sub_82CF69E8);
PPC_FUNC_IMPL(__imp__sub_82AE93D0);
PPC_FUNC_IMPL(__imp__sub_82AF34A8);
PPC_FUNC_IMPL(__imp__sub_82AED028);
PPC_FUNC_IMPL(__imp__sub_82AFAA68);
PPC_FUNC_IMPL(__imp__sub_82AECC08);
PPC_FUNC_IMPL(__imp__sub_82AF2750);
PPC_FUNC_IMPL(__imp__sub_82AFC928);
PPC_FUNC_IMPL(__imp__sub_82AECC98);
PPC_FUNC_IMPL(__imp__sub_82AECAF0);
PPC_FUNC_IMPL(__imp__sub_82AEF370);
PPC_FUNC_IMPL(__imp__sub_82AF2AF8);
PPC_FUNC_IMPL(__imp__sub_82AF6B10);
PPC_FUNC_IMPL(__imp__sub_82AEBE00);
PPC_FUNC_IMPL(__imp__sub_82AE9140);
inline void sub_82893550(PPCContext&, uint8_t*) {}
