// Test fixture for the DAE pass. Mix of cases:
//   - no dead args
//   - exactly one dead arg
//   - multiple dead args
//   - non-static (external linkage) to exercise the linkage guard
// All functions are `static` (internal linkage) except `exported`, so they are
// legitimate DAE candidates. main() calls each so nothing is trivially removed.

// No dead arguments: both a and b are used.
static int allUsed(int a, int b) { return a + b; }

// One dead argument: b is never read.
static int oneDead(int a, int b) { return a + 1; }

// One dead argument, the FIRST one: x is never read.
static int firstDead(int x, int y) { return y * 2; }

// Multiple dead arguments: b and d are never read (a and c are).
static int multiDead(int a, int b, int c, int d) { return a + c; }

// All arguments dead: neither p nor q is read.
static int allDead(int p, int q) { return 42; }

// External linkage: q is dead, but DAE must NOT touch this signature because
// callers outside this module could exist. (Your linkage guard should skip it.)
int exported(int p, int q) { return p - 1; }

int main() {
    return allUsed(1, 2) + oneDead(3, 4) + firstDead(5, 6) +
           multiDead(7, 8, 9, 10) + allDead(11, 12) + exported(13, 14);
}
