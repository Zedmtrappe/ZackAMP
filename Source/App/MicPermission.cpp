#include "MicPermission.h"

#include <juce_core/juce_core.h>

#if ! JUCE_MAC // macOS implementation: MicPermission.mm
namespace af
{
// Other platforms have no such permission. ZACKAMP_FAKE_MIC_DENIED=1 simulates a denial so the
// warning UI can be exercised on development machines.
MicPermission getMicPermission()
{
    return juce::SystemStats::getEnvironmentVariable ("ZACKAMP_FAKE_MIC_DENIED", {}) == "1" ? MicPermission::denied
                                                                                          : MicPermission::granted;
}
void openMicPrivacySettings() {}
} // namespace af
#endif
