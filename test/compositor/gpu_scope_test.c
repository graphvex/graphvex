/* Owner: compositor/gpu_scope.{c,h}, scope.frag and scatter.vert/frag runtime.
 * Actual Vulkan three-scope numeric proof; test-only d64 CPU math is oracle,
 * never the renderer. Covers transparent-domain kernel denominator, linear-light
 * premultiplied alpha, event clips vs element spill, exact backdrop replacement,
 * zero/max radius, bounded budget rejection/retry, borrowed inputs and teardown.
 * GPU waits bounded by production 100ms fence and b.sh runner watchdog. Gaps:
 * genuine stalled queue/device loss/OOM, validation layer and non-macOS hosts.
 * Injected public wait-timeout returns prove retained-job/destroy/retry safety. */
#include "compositor/gpu_scope.h"
#include "darling/test/compositor/filter_gallery_fixture.h"
#include "test_support.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

// Public Vulkan wait seam fault injection; submission still executes on GPU.
// Force a timeout return without claiming the queue has completed. The real
// device function is used after injection to prove safe retained-job recovery.
static unsigned forcedTimeouts;
// Injects bounded wait timeouts before delegating to Vulkan's real fence wait.
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device,u32 count,
    const VkFence *fences,VkBool32 all,u64 timeout) {
    if (forcedTimeouts) { --forcedTimeouts; return VK_TIMEOUT; }
    PFN_vkWaitForFences real=(PFN_vkWaitForFences) vkGetDeviceProcAddr(device,"vkWaitForFences");
    assert(real && real!=vkWaitForFences);
    return real(device,count,fences,all,timeout);
}

enum { WIDTH=12, HEIGHT=10, PX=3, PY=2, PW=6, PH=6, FX=2, FY=3 };
// Converts an encoded channel to linear-light space for the oracle.
static d64 linear(unsigned value) {
    d64 v=value/255.0;
    return v<=.04045 ? v/12.92 : pow((v+.055)/1.055,2.4);
}
// Encodes a linear-light oracle channel as an 8-bit display value.
static unsigned encoded(d64 value) {
    value=fmin(1,fmax(0,value));
    return (unsigned) lround(255*(value<=.0031308 ? 12.92*value : 1.055*pow(value,1/2.4)-.055));
}
// Reads one RGBA pixel and converts its channels to linear light.
static void sample(const Image *image,int x,int y,d64 *out) {
    memset(out,0,4*sizeof(d64));
    if (x<0 || y<0 || x>=(int) Image_width(image) || y>=(int) Image_height(image)) return;
    const u8 *p=Image_pixels(image)+(bytes) y*Image_stride(image)+(bytes) x*4;
    out[3]=p[3]/255.0;
    for (unsigned c=0;c<3;++c) out[c]=linear(p[c])*out[3];
}
// Applies premultiplied source-over composition to oracle pixels.
static void over(const d64 *src,d64 *dest) {
    d64 a=src[3];
    for (unsigned c=0;c<4;++c) dest[c]=src[c]+dest[c]*(1-a);
}
// Reports whether an absolute pixel lies inside the test element rectangle.
static bool inside(int x,int y) { return x>=PX && x<PX+PW && y>=PY && y<PY+PH; }
// Composes decoration and foreground pixels into an isolated group.
static void group(const Image *decoration,const Image *foreground,int x,int y,d64 *out) {
    memset(out,0,4*sizeof(d64));
    if (!inside(x,y)) return;
    sample(decoration,x-PX,y-PY,out);
    d64 fore[4]; sample(foreground,x-FX,y-FY,fore); over(fore,out);
}
// Computes expected filter output for the selected scope and radius.
static void oracle(unsigned scope,unsigned radius,const Image *prior,const Image *decoration,
                   const Image *foreground,int x,int y,d64 *out) {
    d64 filtered[4]={0};
    d64 denominator=(2*radius+1)*(2*radius+1);
    for (int dy=-(int) radius;dy<=(int) radius;++dy) {
        for (int dx=-(int) radius;dx<=(int) radius;++dx) {
            d64 source[4];
            if (scope==GPU_SCOPE_ELEMENT) group(decoration,foreground,x+dx,y+dy,source);
            else if (scope==GPU_SCOPE_FOREGROUND) sample(foreground,x+dx-FX,y+dy-FY,source);
            else sample(prior,x+dx,y+dy,source);
            for (unsigned c=0;c<4;++c) filtered[c]+=source[c]/denominator;
        }
    }
    sample(prior,x,y,out);
    d64 content[4]; group(decoration,foreground,x,y,content);
    if (scope==GPU_SCOPE_ELEMENT) memcpy(content,filtered,sizeof content);
    else if (inside(x,y)) {
        if (scope==GPU_SCOPE_BACKDROP) memcpy(out,filtered,4*sizeof(d64));
        else { sample(decoration,x-PX,y-PY,content); over(filtered,content); }
    }
    over(content,out);
}
// Compares Vulkan scope-filter output to the independent CPU oracle.
int main(void) {
    assert(GpuScope()==nullptr && GpuScope_0()==nullptr && GpuScope_zero()==nullptr);
    assert(GpuScope_destroy(nullptr) && !GpuScope_isPending(nullptr));
    Device *device=Device_create(false);
    if (!Device_isValid(device)) {
        fprintf(stderr,"gpu_scope_test: SKIP Vulkan device unavailable\n");
        Device_destroy(device); return B_TEST_SKIP;
    }
    char directory[2048]; assert(FilterGallery_shaderDirectory(directory,sizeof directory));
    GpuScope *gpu=GpuScope(device,directory,4096); assert(gpu);
    Image *prior=Image_2(WIDTH,HEIGHT),*deco=Image_2(PW,PH),*fore=Image_2(4,4);
    assert(prior && deco && fore);
    assert(Image_ensureShadow(prior,WIDTH,HEIGHT));
    for (unsigned y=0;y<HEIGHT;++y)
        for (unsigned x=0;x<WIDTH;++x)
            FilterGallery_pixel(prior,x,y,(x+y)%2 ? COLOR_RGBA(210,90,40,255) : COLOR_RGBA(30,110,220,255));
    Image_fill(deco,COLOR_RGBA(100,70,190,80));
    Image_fill(fore,COLOR_RGBA(230,170,20,128));
    u8 snapshot[WIDTH*HEIGHT*4]; memcpy(snapshot,Image_pixels(prior),sizeof snapshot);
    const unsigned radii[]={0,1,2,16};
    for (unsigned scope=0;scope<3;++scope) {
        for (unsigned r=0;r<sizeof radii/sizeof radii[0];++r) {
            Image *output=nullptr;
            assert(GpuScope_render(gpu,scope,prior,deco,PX,PY,fore,FX,FY,radii[r],&output));
            assert(!GpuScope_isPending(gpu));
            for (int y=0;y<HEIGHT;++y) {
                for (int x=0;x<WIDTH;++x) {
                    d64 expected[4]; oracle(scope,radii[r],prior,deco,fore,x,y,expected);
                    const u8 *p=Image_pixels(output)+(bytes) y*Image_stride(output)+(bytes) x*4;
                    // Opaque prior keeps final image opaque except backdrop blur
                    // at viewport edges, which only applies inside the inset panel.
                    for (unsigned c=0;c<3;++c) {
                        int want=(int) encoded(expected[3]>0 ? expected[c]/expected[3] : 0);
                        assert(abs((int) p[c]-want)<=2);
                    }
                    assert(abs((int) p[3]-(int) lround(255*expected[3]))<=1);
                }
            }
            Image_destroy(output);
            assert(!memcmp(snapshot,Image_pixels(prior),sizeof snapshot));
        }
    }
    char text[2048]; bool cut;
    GpuScope_toString(gpu,text,sizeof text,&cut); assert(!cut && strstr(text,"Vulkan"));
    GpuScope_toStringStruct(gpu,text,sizeof text,&cut); assert(!cut && strstr(text,"fence=") && strstr(text,"job="));
    GpuScope_toString(nullptr,text,sizeof text,&cut); assert(!strcmp(text,"nullptr"));
    GpuScope_toString(gpu,text,1,&cut); assert(cut && !text[0]);
    FILE *errors=tmpfile(); assert(errors); int saved=dup(STDERR_FILENO); assert(saved>=0);
    assert(dup2(fileno(errors),STDERR_FILENO)>=0);
    Image *unchanged=prior;
    __typeof__(&GpuScope_render) forms[] = {GpuScope_render,GpuScope_renderSampled};
    for (unsigned form = 0; form < 2; ++form) {
        assert(!forms[form](nullptr,0,prior,deco,PX,PY,fore,FX,FY,1,&unchanged));
        assert(!forms[form](gpu,3,prior,deco,PX,PY,fore,FX,FY,1,&unchanged));
        assert(!forms[form](gpu,0,nullptr,deco,PX,PY,fore,FX,FY,1,&unchanged));
        assert(!forms[form](gpu,0,prior,nullptr,PX,PY,fore,FX,FY,1,&unchanged));
        assert(!forms[form](gpu,0,prior,deco,PX,PY,nullptr,FX,FY,1,&unchanged));
        assert(!forms[form](gpu,0,prior,deco,PX,PY,fore,FX,FY,1,nullptr));
        assert(!forms[form](gpu,0,prior,deco,-1,PY,fore,FX,FY,1,&unchanged));
        assert(!forms[form](gpu,0,prior,deco,WIDTH,PY,fore,FX,FY,1,&unchanged));
        assert(!forms[form](gpu,0,prior,deco,PX,PY,fore,FX,FY,17,&unchanged));
        assert(!forms[form](gpu,0,prior,deco,PX,PY,fore,INT32_MAX,FY,1,&unchanged));
        assert(unchanged == prior);
    }
    assert(!GpuScope_3(nullptr,directory,4096));
    assert(!GpuScope_3(device,nullptr,4096));
    assert(!GpuScope_3(device,directory,0));
    assert(!GpuScope_3(device,"/missing/gpu-scope-shaders",4096));
    assert(!GpuScope_3(device,"",4096));
    assert(!GpuScope_3(device,directory,UINT32_MAX));
    Image *empty=Image_0(); assert(empty);
    assert(!GpuScope_render(gpu,0,empty,deco,PX,PY,fore,FX,FY,1,&unchanged));
    Image_destroy(empty);
    Image *wrong=Image_4(WIDTH,HEIGHT,IMAGE_FORMAT_BGRA8,IMAGE_USAGE_NONE); assert(wrong);
    Image_fill(wrong,COLOR_WHITE);
    assert(!GpuScope_render(gpu,0,wrong,deco,PX,PY,fore,FX,FY,1,&unchanged));
    Image_destroy(wrong);
    fflush(stderr); assert(dup2(saved,STDERR_FILENO)>=0); close(saved); rewind(errors);
    unsigned count=0;
    while (fgets(text,sizeof text,errors)) { assert(strstr(text,"[vex]") && strstr(text,"GpuScope")); ++count; }
    assert(count==28); fclose(errors);
    assert(unchanged==prior && !memcmp(snapshot,Image_pixels(prior),sizeof snapshot));
    assert(GpuScope_render(gpu,2,prior,deco,PX,PY,fore,FX,FY,1,&unchanged));
    Image_destroy(unchanged);
    GpuScope *bounded=GpuScope_3(device,directory,WIDTH*HEIGHT); assert(bounded);
    errors=tmpfile(); saved=dup(STDERR_FILENO); assert(errors && saved>=0);
    assert(dup2(fileno(errors),STDERR_FILENO)>=0);
    unchanged=prior;
    assert(!GpuScope_render(bounded,0,prior,deco,PX,PY,fore,FX,FY,1,&unchanged));
    fflush(stderr); assert(dup2(saved,STDERR_FILENO)>=0); close(saved);
    rewind(errors); assert(fgets(text,sizeof text,errors) && strstr(text,"[vex]")); assert(!fgets(text,sizeof text,errors)); fclose(errors);
    assert(unchanged==prior);
    assert(GpuScope_render(bounded,0,prior,deco,PX,PY,fore,FX,FY,0,&unchanged));
    Image_destroy(unchanged); assert(GpuScope_destroy(bounded));
    // Force render timeout then destroy timeout: object/job/device must remain
    // alive. A later valid render retires that job before using new resources.
    errors=tmpfile(); saved=dup(STDERR_FILENO); assert(errors && saved>=0);
    assert(dup2(fileno(errors),STDERR_FILENO)>=0);
    unchanged=prior; forcedTimeouts=2;
    assert(!GpuScope_renderSampled(gpu,2,prior,deco,PX,PY,fore,FX,FY,1,&unchanged));
    assert(unchanged==prior && GpuScope_isPending(gpu));
    assert(!GpuScope_destroy(gpu) && GpuScope_isPending(gpu));
    fflush(stderr); assert(dup2(saved,STDERR_FILENO)>=0); close(saved);
    rewind(errors); assert(fgets(text,sizeof text,errors) && strstr(text,"[vex]")); assert(!fgets(text,sizeof text,errors)); fclose(errors);
    assert(GpuScope_renderSampled(gpu,2,prior,deco,PX,PY,fore,FX,FY,1,&unchanged));
    assert(!GpuScope_isPending(gpu)); Image_destroy(unchanged);
    Image_destroy(fore); Image_destroy(deco); Image_destroy(prior);
    assert(GpuScope_destroy(gpu) && Device_isValid(device));
    printf("gpu_scope_test: PASS actual Vulkan scoped scatter pixels on %s\n",Device_name(device));
    Device_destroy(device); return 0;
}
