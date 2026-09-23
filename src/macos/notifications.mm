#import <Foundation/Foundation.h>
#import <Foundation/NSUserNotification.h>
#import <Cocoa/Cocoa.h>

// TODO: NSUserNotification is deprecated. Use the User Notifications framework instead: https://developer.apple.com/documentation/usernotifications?language=objc
// Deprecated as of macOS 11.0, which the deployment target now targets, so the
// warning would fail the -Werror build. Silenced rather than ported because
// UNUserNotificationCenter requires a bundled, authorized app and would just
// no-op for a plain binary; the TODO above still stands.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
void NotificationsNotifyMacOsInternal(const char *pTitle, const char *pMessage)
{
	NSString* pNsTitle = [NSString stringWithCString:pTitle encoding:NSUTF8StringEncoding];
	NSString* pNsMsg = [NSString stringWithCString:pMessage encoding:NSUTF8StringEncoding];

	NSUserNotification *pNotification = [[NSUserNotification alloc] autorelease];
	pNotification.title = pNsTitle;
	pNotification.informativeText = pNsMsg;
	pNotification.soundName = NSUserNotificationDefaultSoundName;

	[[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:pNotification];

	[NSApp requestUserAttention:NSInformationalRequest]; // use NSCriticalRequest to annoy the user (doesn't stop bouncing)
}
#pragma clang diagnostic pop
