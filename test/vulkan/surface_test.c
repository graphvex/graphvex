// graphvex/test/vulkan/surface_test.c — mirrors src/vulkan/surface.c
//
// A Surface is a host-borrowed destination + ONE retained present Image.
// There is NO swapchain; present() invokes the host-installed blit seam.

#include "vex.h"
#include <stdio.h>

#include "vulkan/surface.h"

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

// a recording host blit: proves Surface_present hands over native + image
static int g_presentCalls = 0;
static any g_seenNative = nullptr;
static Image *g_seenImage = nullptr;
static bool g_blitOk = true;

// Test callback records surface presentation and returns its configured result.
static bool recordPresent(Surface *surface, any userdata) {
    g_presentCalls++;
    (*(int*) userdata)++;
    g_seenNative = Surface_handle(surface);
    g_seenImage = Surface_presentImage(surface);
    return g_blitOk;
}

// a board revalidate step, recorded
// Counts board revalidation callbacks.
static fn countReval(Board *board, any ud) { (fn)board; (*(int*) ud)++; }

// Exercises surface lifecycle, presentation admission, and board revalidation.
int main() {
    Surface *s = Surface((any) 0xCAFE, 100, 50);
    CHECK(s != nullptr);
    CHECK(Surface_isValid(s));
    CHECK(Surface_width(s) == 100 && Surface_height(s) == 50);
    CHECK(Surface_handle(s) == (any) 0xCAFE);   // borrowed, returned verbatim

    // the retained present image exists and is renderable
    Image *img = Surface_presentImage(s);
    CHECK(img != nullptr);
    CHECK(Image_width(img) == 100 && Image_height(img) == 50);
    CHECK((Image_usage(img) & IMAGE_USAGE_RENDER) != 0u);

    // with no seam installed there is nowhere to present -> false
    CHECK(!Surface_present(s));

    // a registered host blit receives the native handle + the present image
    int hits = 0;
    Surface_onPresent(s, recordPresent, &hits);
    CHECK(Surface_present(s));
    CHECK(g_presentCalls == 1 && hits == 1);
    CHECK(g_seenNative == (any) 0xCAFE);
    CHECK(g_seenImage == Surface_presentImage(s));

    // a failing blit propagates -> present reports false
    g_blitOk = false;
    CHECK(!Surface_present(s));
    CHECK(g_presentCalls == 2);

    // re-register clears the seam (NULL callback) -> back to offscreen false
    Surface_onPresent(s, nullptr, nullptr);
    CHECK(!Surface_present(s));
    CHECK(g_presentCalls == 2);          // not called again

    CHECK(Surface_resize(s, 200, 100));
    CHECK(Surface_width(s) == 200 && Surface_height(s) == 100);
    CHECK(Image_width(Surface_presentImage(s)) == 200);

    // revalidation cascade: Surface_revalidate runs each attached board's steps,
    // then presents the finished image
    Surface_onPresent(s, recordPresent, &hits);   // reinstall a passing blit
    Board *bd = Board(100, 50);
    int stepRuns = 0;
    Board_addRevalidator(bd, countReval, &stepRuns);
    Surface_addBoard(s, bd);
    int callsBefore = g_presentCalls;
    Surface_revalidate(s);
    CHECK(stepRuns == 1);                          // board revalidated first
    CHECK(g_presentCalls == callsBefore + 1);      // then presented

    // a detached board is no longer revalidated
    Surface_removeBoard(s, bd);
    Surface_revalidate(s);
    CHECK(stepRuns == 1);
    Board_destroy(bd);

    // null-safe
    Surface_addBoard(nullptr, nullptr);
    Surface_removeBoard(nullptr, nullptr);
    Surface_revalidate(nullptr);          // must not crash

    // a null native handle is a valid offscreen surface
    Surface *off = Surface();
    CHECK(Surface_isValid(off));
    CHECK(Surface_handle(off) == nullptr);
    CHECK(!Surface_present(off));       // still false without a host seam
    Surface_destroy(off);

    // null-safe
    Surface_onPresent(nullptr, recordPresent, nullptr);   // no-op, no crash
    CHECK(!Surface_isValid(nullptr));
    CHECK(Surface_width(nullptr) == 0u);
    CHECK(Surface_handle(nullptr) == nullptr);
    CHECK(Surface_presentImage(nullptr) == nullptr);
    CHECK(!Surface_present(nullptr));
    CHECK(!Surface_resize(nullptr, 4, 4));
    Surface_destroy(nullptr);

    Surface_destroy(s);
    printf("surface_test: ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
