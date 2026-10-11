#import <Foundation/Foundation.h>

#include "latency.h"

static id<NSObject> gs_LatencyActivity = nil;

void MacSetLatencyCriticalActivity(bool Enable)
{
	if(Enable == (gs_LatencyActivity != nil))
		return;

	if(Enable)
	{
		const NSActivityOptions Options = NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical;
		gs_LatencyActivity = [[[NSProcessInfo processInfo] beginActivityWithOptions:Options reason:@"Low latency gameplay"] retain];
	}
	else
	{
		[[NSProcessInfo processInfo] endActivity:gs_LatencyActivity];
		[gs_LatencyActivity release];
		gs_LatencyActivity = nil;
	}
}
