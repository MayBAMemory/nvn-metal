#pragma once
/* 最小替身（syntax_check.py）：只声明 dm/host/hle_audio.c 用到的 */
#include <stdint.h>
typedef int32_t OSStatus; typedef uint32_t UInt32; typedef double Float64; typedef uint32_t AudioUnitRenderActionFlags;
#define noErr 0
typedef struct OpaqueAudioComponent *AudioComponent; typedef struct ComponentInstanceRecord *AudioComponentInstance; typedef AudioComponentInstance AudioUnit;
typedef struct { UInt32 componentType, componentSubType, componentManufacturer, componentFlags, componentFlagsMask; } AudioComponentDescription;
typedef struct { Float64 mSampleRate; UInt32 mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame, mChannelsPerFrame, mBitsPerChannel, mReserved; } AudioStreamBasicDescription;
typedef struct { Float64 mSampleTime; uint64_t mHostTime; Float64 mRateScalar; uint64_t mWordClockTime; char _smpte[24]; UInt32 mFlags, mReserved; } AudioTimeStamp;
typedef struct { UInt32 mNumberChannels, mDataByteSize; void *mData; } AudioBuffer;
typedef struct { UInt32 mNumberBuffers; AudioBuffer mBuffers[1]; } AudioBufferList;
typedef OSStatus (*AURenderCallback)(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32, AudioBufferList *);
typedef struct { AURenderCallback inputProc; void *inputProcRefCon; } AURenderCallbackStruct;
enum { kAudioUnitType_Output = 0x61756f75, kAudioUnitSubType_DefaultOutput = 0x64656620, kAudioUnitSubType_RemoteIO = 0x72696f63, kAudioUnitManufacturer_Apple = 0x6170706c,
       kAudioFormatLinearPCM = 0x6c70636d, kLinearPCMFormatFlagIsSignedInteger = 4, kLinearPCMFormatFlagIsPacked = 8, kLinearPCMFormatFlagIsFloat = 1,
       kAudioUnitProperty_StreamFormat = 8, kAudioUnitProperty_SetRenderCallback = 23, kAudioUnitScope_Global = 0, kAudioUnitScope_Input = 1, kAudioUnitScope_Output = 2 };
AudioComponent AudioComponentFindNext(AudioComponent, const AudioComponentDescription *); OSStatus AudioComponentInstanceNew(AudioComponent, AudioComponentInstance *);
OSStatus AudioComponentInstanceDispose(AudioComponentInstance); OSStatus AudioUnitSetProperty(AudioUnit, UInt32, UInt32, UInt32, const void *, UInt32);
OSStatus AudioUnitInitialize(AudioUnit); OSStatus AudioUnitUninitialize(AudioUnit); OSStatus AudioOutputUnitStart(AudioUnit); OSStatus AudioOutputUnitStop(AudioUnit);
