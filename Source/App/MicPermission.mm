#include "MicPermission.h"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>

namespace af
{
MicPermission getMicPermission()
{
    if (@available (macOS 10.14, *))
    {
        switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
        {
            case AVAuthorizationStatusAuthorized:    return MicPermission::granted;
            case AVAuthorizationStatusNotDetermined: return MicPermission::notDetermined;
            case AVAuthorizationStatusDenied:
            case AVAuthorizationStatusRestricted:    return MicPermission::denied;
        }
    }
    return MicPermission::granted;
}

void openMicPrivacySettings()
{
    NSURL* url = [NSURL URLWithString: @"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"];
    [[NSWorkspace sharedWorkspace] openURL: url];
}
} // namespace af
