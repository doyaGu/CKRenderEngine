#include <stdio.h>

#include "CKFFStateStore.h"

static int g_failures = 0;

#define Check(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++g_failures; } } while (0)

static void ViewProjectionRebuildsOnceThenCaches()
{
    CKFFStateStore s;
    s.Reset();
    s.Projection.Identity();
    s.View.Identity();
    s.MarkViewProjectionDirty();
    Check(s.EnsureViewProjection() == TRUE, "first ensure must rebuild");
    Check(s.EnsureViewProjection() == FALSE, "second ensure must hit cache");
    s.MarkViewProjectionDirty();
    Check(s.EnsureViewProjection() == TRUE, "dirty must force rebuild");
}

int main()
{
    ViewProjectionRebuildsOnceThenCaches();
    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
