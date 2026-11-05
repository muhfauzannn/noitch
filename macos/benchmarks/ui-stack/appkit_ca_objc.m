// Candidate: Objective-C + AppKit + Core Animation. Same as appkit_ca.swift; isolates the cost of the Swift runtime.
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <sys/time.h>

@interface Panel : NSPanel
@end
@implementation Panel
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

static void reportReady(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("READY %f\n", tv.tv_sec + tv.tv_usec / 1e6);
    fflush(stdout);
}

@interface Delegate : NSObject <NSApplicationDelegate>
@property(strong) Panel *panel;
@property(strong) CALayer *pill;
@property BOOL expanded;
@end

@implementation Delegate
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    NSScreen *screen = NSScreen.mainScreen;
    NSSize size = NSMakeSize(480, 160);
    NSRect frame = NSMakeRect(NSMidX(screen.frame) - size.width / 2, NSMaxY(screen.frame) - size.height, size.width, size.height);
    self.panel = [[Panel alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];
    self.panel.opaque = NO;
    self.panel.backgroundColor = NSColor.clearColor;
    self.panel.hasShadow = NO;
    self.panel.level = NSStatusWindowLevel;
    self.panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorFullScreenAuxiliary;
    NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    view.wantsLayer = YES;
    self.panel.contentView = view;
    self.pill = [CALayer layer];
    self.pill.backgroundColor = NSColor.blackColor.CGColor;
    self.pill.anchorPoint = CGPointMake(0.5, 1);
    self.pill.position = CGPointMake(size.width / 2, size.height);
    self.pill.bounds = CGRectMake(0, 0, 200, 32);
    self.pill.cornerRadius = 12;
    self.pill.cornerCurve = kCACornerCurveContinuous;
    self.pill.maskedCorners = kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner;
    [view.layer addSublayer:self.pill];
    [self.panel orderFrontRegardless];
    [CATransaction flush];
    dispatch_async(dispatch_get_main_queue(), ^{ reportReady(); });
    if ([NSProcessInfo.processInfo.environment[@"BENCH_MODE"] isEqual:@"animate"]) {
        [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *t) { [self toggle]; }];
    }
}

- (void)toggle {
    self.expanded = !self.expanded;
    CGRect bounds = CGRectMake(0, 0, self.expanded ? 440 : 200, self.expanded ? 140 : 32);
    CGFloat radius = self.expanded ? 24 : 12;
    CALayer *from = self.pill.presentationLayer ?: self.pill;
    CASpringAnimation *a = [[CASpringAnimation alloc] initWithPerceptualDuration:0.4 bounce:0.2];
    a.keyPath = @"bounds";
    a.duration = a.settlingDuration;
    a.fromValue = [NSValue valueWithRect:from.bounds];
    a.toValue = [NSValue valueWithRect:bounds];
    [self.pill addAnimation:a forKey:@"bounds"];
    CASpringAnimation *b = [[CASpringAnimation alloc] initWithPerceptualDuration:0.4 bounce:0.2];
    b.keyPath = @"cornerRadius";
    b.duration = b.settlingDuration;
    b.fromValue = @(from.cornerRadius);
    b.toValue = @(radius);
    [self.pill addAnimation:b forKey:@"cornerRadius"];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.pill.bounds = bounds;
    self.pill.cornerRadius = radius;
    [CATransaction commit];
}
@end

int main(void) {
    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        Delegate *delegate = [Delegate new];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
