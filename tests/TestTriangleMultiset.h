#ifndef CKRE_TEST_TRIANGLE_MULTISET_H
#define CKRE_TEST_TRIANGLE_MULTISET_H

#include <stdio.h>
#include <setjmp.h>

#include "CKTypes.h"
#include "XArray.h"

struct TestTriKey {
    int v[3];
};

struct TestTriCount {
    TestTriKey key;
    int count;
};

static jmp_buf g_TestJump;
static const char *g_TestFailureMessage = nullptr;

inline void TestFail(const char *message) {
    g_TestFailureMessage = message;
    longjmp(g_TestJump, 1);
}

inline void TestCheck(bool condition, const char *message) {
    if (!condition)
        TestFail(message);
}

inline void TestSortTriKey(TestTriKey &key) {
    if (key.v[0] > key.v[1]) {
        int tmp = key.v[0];
        key.v[0] = key.v[1];
        key.v[1] = tmp;
    }
    if (key.v[1] > key.v[2]) {
        int tmp = key.v[1];
        key.v[1] = key.v[2];
        key.v[2] = tmp;
    }
    if (key.v[0] > key.v[1]) {
        int tmp = key.v[0];
        key.v[0] = key.v[1];
        key.v[1] = tmp;
    }
}

inline TestTriKey TestMakeTriKey(int a, int b, int c) {
    TestTriKey key = {{a, b, c}};
    TestSortTriKey(key);
    return key;
}

inline bool TestTriKeyEquals(const TestTriKey &lhs, const TestTriKey &rhs) {
    return lhs.v[0] == rhs.v[0] && lhs.v[1] == rhs.v[1] && lhs.v[2] == rhs.v[2];
}

inline void TestAddTriangle(XArray<TestTriCount> &triangles, const TestTriKey &key) {
    for (int i = 0; i < triangles.Size(); ++i) {
        if (TestTriKeyEquals(triangles[i].key, key)) {
            ++triangles[i].count;
            return;
        }
    }

    TestTriCount item;
    item.key = key;
    item.count = 1;
    triangles.PushBack(item);
}

inline bool TestSameTriangleMultiset(const XArray<TestTriCount> &lhs, const XArray<TestTriCount> &rhs) {
    if (lhs.Size() != rhs.Size())
        return false;

    for (int i = 0; i < lhs.Size(); ++i) {
        bool found = false;
        for (int j = 0; j < rhs.Size(); ++j) {
            if (TestTriKeyEquals(lhs[i].key, rhs[j].key)) {
                if (lhs[i].count != rhs[j].count)
                    return false;
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    return true;
}

inline void TestBuildTriangleMultisetFromTriList(const XArray<CKWORD> &triIndices, XArray<TestTriCount> &triangles) {
    triangles.Resize(0);
    TestCheck((triIndices.Size() % 3) == 0, "Input triangle index list must be multiple of 3");

    for (int i = 0; i < triIndices.Size(); i += 3) {
        TestAddTriangle(triangles, TestMakeTriKey((int)triIndices[i + 0], (int)triIndices[i + 1], (int)triIndices[i + 2]));
    }
}

struct TestFramework {
    int total;
    int passed;

    TestFramework() : total(0), passed(0) {}

    void Run(const char *name, void (*fn)()) {
        ++total;
        printf("Running test: %s... ", name);
        fflush(stdout);
        g_TestFailureMessage = nullptr;
        if (setjmp(g_TestJump) == 0) {
            fn();
            ++passed;
            printf("PASSED\n");
        } else {
            printf("FAILED: %s\n", g_TestFailureMessage ? g_TestFailureMessage : "unknown error");
        }
    }

    int ExitCode() const {
        printf("\n=== Test Summary ===\n");
        printf("Total tests: %d\n", total);
        printf("Passed: %d\n", passed);
        printf("Failed: %d\n", total - passed);
        return (passed == total) ? 0 : 1;
    }
};

#endif // CKRE_TEST_TRIANGLE_MULTISET_H
