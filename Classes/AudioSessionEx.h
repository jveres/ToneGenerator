//
//  AudioSessionEx.h
//

#import <Foundation/Foundation.h>

#define SAMPLE_RATE 44100.0
#define AUDIO_BUFFER_LEN 4096

@interface AudioSessionEx : NSObject

@property (nonatomic, copy) void (^onReceive)(unsigned int);
@property (nonatomic, copy) void (^onComplete)(BOOL);
@property (readonly) float RXLevel;
@property (readonly) float TXLevel;

+ (AudioSessionEx *)shared;

- (void)setSessionActive:(BOOL)active;
- (void)startListener:(void(^)(unsigned int code))reception;
- (void)stopListener;
- (void)broadcast:(unsigned int)code completion:(void(^)(BOOL success))completion;

@end
