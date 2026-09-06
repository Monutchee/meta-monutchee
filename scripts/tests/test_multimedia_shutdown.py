"""Compile the actual U-Boot patch's shutdown code against a fake EEMI/MMIO backend."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "meta-zynqmp-addon/recipes-bsp/u-boot/files/0001-zynqmp-multimedia-off.patch"

STUB = r'''
#ifndef MM_TEST_H
#define MM_TEST_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
typedef uint32_t u32;
typedef uint32_t fdt32_t;
#define cpu_to_fdt32(v) (v)
static int fdt_find_or_add_subnode(void *blob,int node,const char *name)
{ (void)blob; (void)node; (void)name; return 0; }
static int fdt_setprop(void *b,int n,const char *s,const void *v,int len)
{ (void)b; (void)n; (void)s; (void)v; (void)len; return 0; }
#define __iomem
#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((~0U >> (31 - (h))) & (~0U << (l)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define PAYLOAD_ARG_CNT 7
enum { PM_GET_NODE_STATUS=3, PM_REQUEST_NODE=13, PM_RELEASE_NODE=14,
       PM_SET_REQUIREMENT=15, PM_CLOCK_ENABLE=36, PM_CLOCK_DISABLE=37,
       PM_CLOCK_GETSTATE=38 };
enum { NODE_GPU_PP_0=20, NODE_GPU_PP_1=21, NODE_DP=41, NODE_GPU=58 };
static u32 nodes[64][3], clocks[128], dpregs[256], dmaregs[512];
static int denied_clock, pm_error, clock_writes, stall_dma, keep_vpll, implicit_requirement;
static int xilinx_pm_request(u32 api,u32 a,u32 b,u32 c,u32 d,u32 e,u32 f,u32 *r)
{
    (void)c; (void)d; (void)e; (void)f;
    memset(r,0,PAYLOAD_ARG_CNT*sizeof(*r));
    if (pm_error && api == PM_REQUEST_NODE) return pm_error;
    switch(api) {
    case PM_GET_NODE_STATUS: memcpy(r+1,nodes[a],3*sizeof(u32)); break;
    case PM_REQUEST_NODE: case PM_SET_REQUIREMENT:
        nodes[a][2]=1; nodes[a][1]=b; nodes[a][0]=!!b || implicit_requirement;
        /* Combined GPU power does not update legacy PP state machines. */
        break;
    case PM_RELEASE_NODE:
        nodes[a][1]=nodes[a][2]=0;
        if(a==NODE_DP && !keep_vpll) clocks[96]=0;
        break;
    case PM_CLOCK_ENABLE: case PM_CLOCK_DISABLE:
        clock_writes++;
        assert(a==16 || a==17 || a==18 || a==20 || a==24 || a==25 || a==26);
        /* A caller must still own the node when modifying a clock. */
        assert(nodes[a>=24 ? NODE_GPU : NODE_DP][2]==1);
        if((int)a!=denied_clock) clocks[a]=(api==PM_CLOCK_ENABLE);
        break;
    case PM_CLOCK_GETSTATE:
        if (a==1) return -EOPNOTSUPP; /* VPLL output is a mux, not the PLL. */
        r[1]=clocks[a]; break;
    default: assert(!"unexpected firmware API");
    }
    return 0;
}
static u32 *mm_reg(const void *p)
{
    uintptr_t a=(uintptr_t)p;
    if(a>=0xfd4a0000 && a<0xfd4a0400) return &dpregs[(a-0xfd4a0000)/4];
    assert(a>=0xfd4c0000 && a<0xfd4c0800);
    return &dmaregs[(a-0xfd4c0000)/4];
}
static u32 readl(const void *p) { return *mm_reg(p); }
static void writel(u32 val,void *p)
{
    u32 *reg=mm_reg(p);
    *reg=val;
    if((uintptr_t)p>=0xfd4c0000 && ((uintptr_t)p & 0xff)==0x18 && !stall_dma)
        reg[1]=0; /* Outstanding DMA drains after PAUSE. */
}
static void udelay(unsigned int usec) { (void)usec; }
static void reset(void)
{
    memset(nodes,0,sizeof(nodes)); memset(clocks,0,sizeof(clocks));
    memset(dpregs,0,sizeof(dpregs)); memset(dmaregs,0,sizeof(dmaregs));
    nodes[58][0]=nodes[20][0]=nodes[21][0]=1;
    clocks[24]=clocks[25]=clocks[26]=clocks[96]=1;
    denied_clock=-1; pm_error=clock_writes=stall_dma=keep_vpll=implicit_requirement=0;
}
#endif
'''

MAIN = r'''
int main(void)
{
    reset();
    assert(mm_gpu_off()==0);
    assert(!clocks[24] && !clocks[25] && !clocks[26]);
    assert(!nodes[58][2] && !nodes[20][0] && !nodes[21][0]);
    /* Model late firmware cleanup: a stale PP state would restore GPU_REF. */
    for (int n=20; n<=21; n++) if (nodes[n][0]) clocks[24]=1;
    assert(!clocks[24]);
    reset(); nodes[20][2]=nodes[21][2]=1;
    assert(mm_gpu_off()==0 && !nodes[20][2] && !nodes[21][2]);
    reset(); nodes[58][2]=2;
    assert(mm_gpu_off()==-EBUSY && !clock_writes);
    reset(); nodes[20][2]=2;
    assert(mm_gpu_off()==-EBUSY && !clock_writes);
    /* Default requirements can keep a node on without a visible usage bit.
     * Refuse clock gating when the FSBL ownership policy has not been applied. */
    reset(); implicit_requirement=1;
    assert(mm_gpu_off()==-EBUSY && !clock_writes && !nodes[58][2]);
    reset(); pm_error=2002;
    assert(mm_gpu_off()!=0 && !clock_writes);
    reset(); denied_clock=24;
    assert(mm_gpu_off()==-EIO && clocks[24] && !nodes[58][2]);
    reset();
    dmaregs[0x218/4]=1; dmaregs[0x21c/4]=1U<<21;
    assert(mm_display_off()==0);
    assert(!clocks[16] && !clocks[17] && !clocks[18] && !clocks[20] && !clocks[96]);
    assert(!nodes[41][2] && dpregs[0x238/4]==15 && !dpregs[0x80/4]);
    assert(!(dmaregs[0x218/4]&1));
    reset(); nodes[41][2]=2;
    assert(mm_display_off()==-EBUSY && !clock_writes);
    reset(); nodes[41][2]=1; /* Preexisting U-Boot owner. */
    assert(mm_display_off()==0 && !nodes[41][2]);
    reset(); stall_dma=1;
    dmaregs[0x218/4]=1; dmaregs[0x21c/4]=1U<<21;
    assert(mm_display_off()==-ETIMEDOUT);
    assert(clocks[20] && nodes[41][2] && (dmaregs[0x218/4]&1));
    reset(); keep_vpll=1;
    assert(mm_display_off()==0 && clocks[96]); /* Never force a shared PLL. */
    puts("shutdown safety cases passed");
    return 0;
}
'''

class ShutdownTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "requires a host C compiler")
    def test_actual_shutdown_code_with_firmware_failures_and_dma(self):
        patch = PATCH.read_text()
        section = patch.split("+++ b/board/xilinx/zynqmp/multimedia.c\n", 1)[1]
        code = "\n".join(line[1:] for line in section.splitlines() if line.startswith("+"))
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            for name in ("fdt_support.h", "linux/libfdt.h", "asm/io.h", "linux/bitops.h", "linux/delay.h", "linux/kernel.h", "zynqmp_firmware.h"):
                header = work / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "mm_test.h"\n')
            (work / "mm_test.h").write_text(STUB)
            source = work / "test.c"
            source.write_text(code + "\n" + MAIN)
            executable = work / "test"
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(work), str(source), "-o", str(executable)], check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
            self.assertIn("shutdown safety cases passed", result.stdout)

if __name__ == "__main__":
    unittest.main()
