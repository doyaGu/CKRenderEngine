#include "CKRenderProfile.h"

// Disabled annotations must not evaluate even their arguments or require NVTX.
// Deliberately undefined names also catch accidental SDK/argument dependencies.
int main() {
    CKRE_PROFILE_SCOPE(UndefinedProfileName());
    CKRE_PROFILE_VALUE(UndefinedProfileName(), UndefinedProfileValue());
    int calls = 0;
    const int result = CKRE_PROFILE_CALL(UndefinedProfileName(), ++calls);
    return calls == 1 && result == 1 ? 0 : 1;
}
