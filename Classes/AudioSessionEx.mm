//
//  AudioSessionEx.m
//

#import "AudioSessionEx.h"
#import <AudioToolbox/AudioToolbox.h>
#import "TPCircularBuffer.h"
#import "AudioEx.h"

@implementation AudioSessionEx
{
    AudioComponentInstance inputUnit;
    AudioComponentInstance outputUnit;
    TPCircularBuffer buffer;
    AudioEx *audio_ex;
    BOOL _audio_sampler_active;
    BOOL _audio_session_is_active;
}

@synthesize onReceive, onComplete, RXLevel, TXLevel;

+ (AudioSessionEx *)shared
{
    static dispatch_once_t once;
    static AudioSessionEx *audiosession_ex = nil;
    dispatch_once(&once, ^{
        audiosession_ex = [[AudioSessionEx alloc] init];
    });
    return audiosession_ex;
}

+ (dispatch_queue_t)queue
{
    static dispatch_once_t pred = 0;
    static dispatch_queue_t _queue;
    dispatch_once(&pred, ^{
        _queue = dispatch_queue_create("audio_session_ex.gdc.queue", NULL);
        dispatch_queue_t high = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
        dispatch_set_target_queue(_queue, high);
    });
    return _queue;
}

static AUDIO_DATA audio_data;

static inline OSStatus AudioOutputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
	AudioSessionEx *THIS = (AudioSessionEx *)inRefCon;
    int bytesToCopy = ioData->mBuffers[0].mDataByteSize;
    SInt16 *targetBuffer = (SInt16 *)ioData->mBuffers[0].mData;
    memset(targetBuffer, 0, bytesToCopy);
    UInt32 framesGenerated = 0;
    BOOL generating = (THIS->audio_ex->signal_generator.remaining > 0) ? YES : THIS->audio_ex->signal_generator_data(audio_data);
    while (generating && framesGenerated < inNumberFrames)
    {
        if (THIS->audio_ex->signal_generator.remaining > 0)
        {
            // process samples
            UInt32 frameCount = MIN(inNumberFrames - framesGenerated, THIS->audio_ex->signal_generator.remaining);
            for (UInt32 i = 0; i < frameCount; i++)
            {
                int sampleNum = THIS->audio_ex->signal_generator.length - THIS->audio_ex->signal_generator.remaining;
                
                double sig = sin(2.0 * M_PI * THIS->audio_ex->signal_generator.carrier * (double)sampleNum / SAMPLE_RATE + THIS->audio_ex->signal_generator.phase);
                double amp = sin(M_PI / THIS->audio_ex->signal_generator.length * (double)sampleNum);
                
                targetBuffer[framesGenerated + i] =  (SInt16)(INT16_MAX * amp * sig);
                THIS->audio_ex->signal_generator.remaining--;
            }
            framesGenerated += frameCount;
        } else
        {
            // generate new samples
            generating = THIS->audio_ex->signal_generator_data(audio_data);
        }
    }
    if (!generating)
    {
        AudioOutputUnitStop(THIS->outputUnit);
        if (THIS.onComplete)
        {
            dispatch_async([AudioSessionEx queue], ^{
                THIS.onComplete(YES);
                THIS.onComplete = nil;
            });
        }
    }
	return noErr;
}

static inline void ToneInterruptionCallback(void *inClientData, UInt32 inInterruptionState)
{
    AudioSessionEx *audiosession_ex = (AudioSessionEx *)inClientData;
    
    static bool audio_was_active = NO;
    if (inInterruptionState == kAudioSessionEndInterruption)
    {
        if (audio_was_active) [audiosession_ex setSessionActive:YES];
    }
    else if (inInterruptionState == kAudioSessionBeginInterruption)
    {
        audio_was_active = audiosession_ex->_audio_session_is_active;
        if (audio_was_active) [audiosession_ex setSessionActive:NO];
    }
}

- (void)_createAudioOutputUnit
{
	AudioComponentDescription defaultOutputDescription;
	defaultOutputDescription.componentType = kAudioUnitType_Output;
	defaultOutputDescription.componentSubType = kAudioUnitSubType_RemoteIO;
	defaultOutputDescription.componentManufacturer = kAudioUnitManufacturer_Apple;
	defaultOutputDescription.componentFlags = 0;
	defaultOutputDescription.componentFlagsMask = 0;
	AudioComponent defaultOutput = AudioComponentFindNext(NULL, &defaultOutputDescription);
	AudioComponentInstanceNew(defaultOutput, &outputUnit);
	AURenderCallbackStruct input;
	input.inputProc = AudioOutputCallback;
	input.inputProcRefCon = self;
	AudioUnitSetProperty(outputUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &input, sizeof(input));
    // output format: 16 bit, single channel, fixed point, linear PCM
	AudioStreamBasicDescription streamFormat;
	streamFormat.mSampleRate = SAMPLE_RATE;
	streamFormat.mFormatID = kAudioFormatLinearPCM;
	streamFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
	streamFormat.mBytesPerPacket = 2;
	streamFormat.mFramesPerPacket = 1;
	streamFormat.mBytesPerFrame = 2;
	streamFormat.mChannelsPerFrame = 1;
	streamFormat.mBitsPerChannel = 16;
    streamFormat.mReserved = 0;
	AudioUnitSetProperty(outputUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &streamFormat, sizeof(AudioStreamBasicDescription));
    AudioUnitInitialize(outputUnit);
}

static inline OSStatus AudioInputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)

{
	AudioSessionEx *THIS = (AudioSessionEx *)inRefCon;
    AudioBuffer buffer;
	buffer.mNumberChannels = 1;
	buffer.mDataByteSize = inNumberFrames * sizeof(Float32);
	buffer.mData = malloc(buffer.mDataByteSize);
	AudioBufferList bufferList;
	bufferList.mNumberBuffers = 1;
	bufferList.mBuffers[0] = buffer;
    OSStatus err = AudioUnitRender(THIS->inputUnit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, &bufferList);
    if (!err) TPCircularBufferProduceBytes(&THIS->buffer, bufferList.mBuffers[0].mData, buffer.mDataByteSize);
    else printf("Error sampling audio data\n"); // DEBUG
    free(bufferList.mBuffers[0].mData);
    return err;
}

- (void)_createAudioInputUnit
{
	AudioComponentDescription desc;
	desc.componentType = kAudioUnitType_Output;
	desc.componentSubType = kAudioUnitSubType_RemoteIO;
	desc.componentFlags = 0;
	desc.componentFlagsMask = 0;
	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	AudioComponent inputComponent = AudioComponentFindNext(NULL, &desc);
	AudioComponentInstanceNew(inputComponent, &inputUnit);
	UInt32 flag = 1;
	AudioUnitSetProperty(inputUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &flag, sizeof(flag));
    // input format: 32 bit, single channel, floating point, linear PCM
	AudioStreamBasicDescription streamFormat;
	streamFormat.mSampleRate = SAMPLE_RATE;
	streamFormat.mFormatID = kAudioFormatLinearPCM;
	streamFormat.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
	streamFormat.mBytesPerPacket = 4;
	streamFormat.mFramesPerPacket = 1;
	streamFormat.mBytesPerFrame = 4;
	streamFormat.mChannelsPerFrame = 1;
	streamFormat.mBitsPerChannel = 32;
    streamFormat.mReserved = 0;
	AudioUnitSetProperty(inputUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &streamFormat, sizeof(streamFormat));
	AURenderCallbackStruct callbackStruct;
	callbackStruct.inputProc = AudioInputCallback;
	callbackStruct.inputProcRefCon = self;
	AudioUnitSetProperty(inputUnit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 1, &callbackStruct, sizeof(callbackStruct));
	flag = 0;
	AudioUnitSetProperty(inputUnit, kAudioUnitProperty_ShouldAllocateBuffer, kAudioUnitScope_Output, 1, &flag, sizeof(flag));
    AudioUnitInitialize(inputUnit);
}

- (float)RXLevel
{
    return audio_ex->rx_level;
}

- (float)TXLevel
{
    Float32 volume;
    UInt32 dataSize = sizeof(Float32);
    AudioSessionGetProperty(kAudioSessionProperty_CurrentHardwareOutputVolume, &dataSize, &volume);
    return volume;
}

- (id)init
{
    if (self = [super init])
    {
        [self _createAudioInputUnit];
        [self _createAudioOutputUnit];
        TPCircularBufferInit(&buffer, AUDIO_BUFFER_LEN);
        audio_ex = new AudioEx(SAMPLE_RATE);
    }
    return self;
}

- (void)setSessionActive:(BOOL)active
{
    _audio_session_is_active = active;
    if (active)
    {
        OSStatus result = AudioSessionInitialize(NULL, NULL, ToneInterruptionCallback, self);
        if (result == kAudioSessionNoError)
        {
            UInt32 sessionCategory = kAudioSessionCategory_PlayAndRecord;
            AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof(sessionCategory), &sessionCategory);
            Float32 preferredBufferSize = .005;
            AudioSessionSetProperty(kAudioSessionProperty_PreferredHardwareIOBufferDuration, sizeof(preferredBufferSize), &preferredBufferSize);
            AudioSessionSetActive(true);
        }
    } else {
        [self stopListener];
        AudioSessionSetActive(false);
    }
}

#define IPHONE5_AUDIO_INPUT_LAG 15

- (void)_audio_sampler
{
    dispatch_time_t delay = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC * 0.01); // 10ms
    dispatch_after(delay, [AudioSessionEx queue], ^(void) {
        int32_t availableBytes = 0;
        Float32 *samples = (Float32 *)TPCircularBufferTail(&buffer, &availableBytes);
        int32_t availableSamples = availableBytes / sizeof(Float32);
        // DEBUG
        //printf("%i\n", availableSamples);
        if (availableSamples >= SAMPLING_LENGTH)
        {
            static int samples_count = 0;
            if (samples_count < IPHONE5_AUDIO_INPUT_LAG) samples_count++;
            else audio_ex->gft(samples);
            
            TPCircularBufferConsume(&buffer, SAMPLING_LENGTH * sizeof(Float32));
        }
        if (audio_ex->result > 0)
        {
            if (self.onReceive)
            {
                __block unsigned int result = audio_ex->result;
                dispatch_async([AudioSessionEx queue], ^{ self.onReceive(result); });
            }
            audio_ex->result = 0;
        }
        if (_audio_sampler_active) [self _audio_sampler];
    });
}

- (void)startListener:(void(^)(unsigned int code))reception
{
    self.onReceive = reception;
    if (inputUnit)
    {
        AudioOutputUnitStart(inputUnit);
        _audio_sampler_active = YES;
        [self _audio_sampler];
    }
}

- (void)stopListener
{
    if (inputUnit)
    {
        AudioOutputUnitStop(inputUnit);
        _audio_sampler_active = NO;
        self.onReceive = nil;
    }
}

- (void)broadcast:(unsigned int)code completion:(void(^)(BOOL success))completion
{
    self.onComplete = completion;
    if (outputUnit)
    {
        audio_ex->signal_generator_data_from_int(code, audio_data);
        AudioOutputUnitStart(outputUnit);
    } else {
        if (self.onComplete)
        {
            self.onComplete(NO);
            self.onComplete = nil;
        }
    }
}

- (void)dealloc
{
    [self setSessionActive:NO];
    if (inputUnit)
    {
        AudioOutputUnitStop(inputUnit);
        AudioUnitUninitialize(inputUnit);
        AudioComponentInstanceDispose(inputUnit);
    }
    if (outputUnit)
    {
        AudioOutputUnitStop(outputUnit);
        AudioUnitUninitialize(outputUnit);
        AudioComponentInstanceDispose(outputUnit);
    }
    TPCircularBufferCleanup(&buffer);
    delete[] audio_ex;
    [super dealloc];
}

@end
