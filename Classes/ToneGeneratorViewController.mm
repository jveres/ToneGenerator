//
//  ToneGeneratorViewController.m
//  ToneGenerator
//

#import "ToneGeneratorViewController.h"
#import "AudioSessionEx.h"
#import "SKBounceAnimation.h"
#import <QuartzCore/QuartzCore.h>
#import <MediaPlayer/MediaPlayer.h>

@implementation ToneGeneratorViewController
{
    AudioSessionEx *ase;
    BOOL repeat;
}

@synthesize playButton, generateButton, textView, repeatSwitch, txView, rxView;

static unsigned int code = 0;

- (IBAction)broadcastAction:(UIButton *)button
{
    self.playButton.enabled = NO;
    [ase broadcast:code completion:^(BOOL success)
    {
        dispatch_async(dispatch_get_main_queue(),^{
            self.playButton.enabled = YES;
        });
    }];
}

- (IBAction)generateAction:(UIButton *)button
{
    [self generateCode];
}

- (IBAction)repeatAction:(UISwitch *)_switch
{
    repeat = _switch.on;
}

- (void)generateCode
{
    code = arc4random();
    [self.generateButton setTitle:[NSString stringWithFormat:@"0x%08X", code] forState:UIControlStateNormal];
}

- (void)bounce:(UIView*)view
{
    // bounce effect
    CATransform3D transform = view.layer.transform;
    id finalValue = [NSValue valueWithCATransform3D: CATransform3DScale(transform, 1.035, 1.035, 1.035)];
    
    SKBounceAnimation *bounceAnimation = [SKBounceAnimation animationWithKeyPath:@"transform"];
    bounceAnimation.fromValue = finalValue;
    bounceAnimation.toValue = [NSValue valueWithCATransform3D:transform];
    bounceAnimation.duration = 0.5f;
    bounceAnimation.numberOfBounces = 4;
    bounceAnimation.shake = YES;
    
    [view.layer addAnimation:bounceAnimation forKey:@"key"];
}

static BOOL isProxyActive = NO;

- (void)_proximity
{
    dispatch_time_t delay = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC * 0.1); // 100ms
    dispatch_after(delay, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^(void) {
        if (!isProxyActive && [UIDevice currentDevice].proximityState && self.playButton.enabled)
        {
            isProxyActive = YES;
            [self broadcastAction:self.playButton];
        } else {
            isProxyActive = [UIDevice currentDevice].proximityState;
        }
        [self _proximity];
    });
}

- (void)_broadcastRepeat
{
    dispatch_time_t delay = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC * 2.5); // 2.5sec
    dispatch_after(delay, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^(void) {
        if (repeat && self.playButton.enabled)
        {
            dispatch_async(dispatch_get_main_queue(),^{
                [self broadcastAction:self.playButton];
            });
        }
        [self _broadcastRepeat];
    });
}

- (void)_txmeter
{
    dispatch_time_t delay = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC * 0.1); // 100ms
    dispatch_after(delay, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^(void) {
        float tx = ase.TXLevel;
        CGRect frame = txView.frame;
        const float maxWidth = 85.0;
        frame.size.width = MAX(maxWidth * tx, 6);
        dispatch_async(dispatch_get_main_queue(),^{
            [UIView beginAnimations:nil context:nil];
            [UIView setAnimationDuration:0.1];
            txView.frame = frame;
            [UIView commitAnimations];
        });
        [self _txmeter];
    });
}


- (void)_rxmeter
{
    dispatch_time_t delay = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC * 0.05); // 50ms
    dispatch_after(delay, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^(void) {
        float rx = ase.RXLevel;
        CGRect frame = rxView.frame;
        const float maxWidth = 85.0;
        frame.size.width = MAX(maxWidth * rx, 6);
        dispatch_async(dispatch_get_main_queue(),^{
            [UIView beginAnimations:nil context:nil];
            [UIView setAnimationDuration:0.1];
            rxView.frame = frame;
            [UIView commitAnimations];
        });
        [self _rxmeter];
    });
}

#define PROXIMITY_ENABLED NO

- (void)viewDidLoad
{
	[super viewDidLoad];
    
    [UIApplication sharedApplication].idleTimerDisabled = YES;
    
    [UIDevice currentDevice].proximityMonitoringEnabled = PROXIMITY_ENABLED;
    if (PROXIMITY_ENABLED) [self _proximity];
    
    [self _broadcastRepeat];
    [self _txmeter];
    [self _rxmeter];
    for (int i=1; i<=4; i++)
    {
        UIView *v = [self.view viewWithTag:i];
        v.layer.cornerRadius = 3.0;
    }
    self.textView.layer.cornerRadius = 8.0;
    
    [self generateCode];
    
    [self.playButton setTitle:@"Broadcast" forState:UIControlStateNormal];
    [self.playButton setTitleColor:[UIColor grayColor] forState:UIControlStateDisabled];
    [self.playButton setTitle:@"Wait..." forState:UIControlStateDisabled];
    
    ase = [AudioSessionEx shared];
    [ase setSessionActive:YES];
    
    [ase startListener:^(unsigned int _code) {
        dispatch_async(dispatch_get_main_queue(),^{
            self.textView.text = [NSString stringWithFormat:@"0x%08X\n%@", _code, self.textView.text];
            [self bounce:self.textView];
            if (_code != code)
            {
                self.generateButton.titleLabel.textColor = [UIColor redColor];
                [self bounce:self.generateButton];
            }
        });
    }];
}

- (void)viewDidUnload
{
	self.playButton = nil;
    self.generateButton = nil;
    [ase setSessionActive:NO];
}

@end
