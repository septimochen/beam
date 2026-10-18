#pragma once
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, BeamContentKind) { BeamContentKindFile = 1, BeamContentKindText };
typedef NS_ENUM(NSInteger, BeamEventKind) {
    BeamEventKindListening,
    BeamEventKindProgress,
    BeamEventKindCompleted,
    BeamEventKindFailed,
    BeamEventKindWarning,
    BeamEventKindStopped
};
typedef NS_ENUM(NSInteger, BeamTransferStage) {
    BeamTransferStageHashing,
    BeamTransferStageConnecting,
    BeamTransferStageWaiting,
    BeamTransferStageTransferring,
    BeamTransferStageVerifying,
    BeamTransferStageComplete
};

// Value snapshots only: no C++ objects, pointers, or worker callbacks enter Swift.
@interface BeamDesktopEvent : NSObject
@property(nonatomic, readonly) BOOL sending;
@property(nonatomic, readonly) BOOL checking;
@property(nonatomic, readonly) BeamEventKind kind;
@property(nonatomic, readonly) BeamContentKind contentKind;
@property(nonatomic, readonly) BeamTransferStage stage;
@property(nonatomic, readonly, copy) NSString* message;
@property(nonatomic, readonly, copy) NSString* filename;
@property(nonatomic, readonly) uint64_t bytes;
@property(nonatomic, readonly) uint64_t total;
@property(nonatomic, readonly) double bytesPerSecond;
@property(nonatomic, readonly, copy) NSString* text;
@end

// All methods are called on the application's main thread. Workers remain owned
// by the shared C++ service; shutdown cancels and joins before releasing it.
@interface BeamDesktopBridge : NSObject
- (nullable instancetype)initWithStateDirectory:(nullable NSString*)directory
                                           port:(NSUInteger)port
                                      advertise:(BOOL)advertise
                                          error:(NSError**)error
    NS_SWIFT_NAME(init(stateDirectory:port:advertise:));
- (nullable instancetype)initWithStateDirectory:(nullable NSString*)directory
                                           port:(NSUInteger)port
                                      advertise:(BOOL)advertise
                                        timeout:(NSUInteger)timeout
                                          error:(NSError**)error
    NS_SWIFT_NAME(init(stateDirectory:port:advertise:timeout:));
- (instancetype)init NS_UNAVAILABLE;
@property(nonatomic, readonly) BOOL sending;
@property(nonatomic, readonly) BOOL receiving;
@property(nonatomic, readonly) BOOL checking;
@property(nonatomic, readonly, copy) NSString* version;
- (nullable NSArray<NSString*>*)peersAndReturnError:(NSError**)error;
- (BOOL)unpairPeer:(NSString*)peer error:(NSError**)error;
- (BOOL)sendToPeer:(NSString*)peer
              kind:(BeamContentKind)kind
              file:(NSString*)file
           content:(NSString*)content
             error:(NSError**)error NS_SWIFT_NAME(send(peer:kind:file:content:));
- (BOOL)startReceivingInDirectory:(NSString*)directory
                            error:(NSError**)error NS_SWIFT_NAME(startReceiving(directory:));
- (void)cancelSend;
- (void)stopReceiving;
- (BOOL)checkPeer:(NSString*)peer error:(NSError**)error;
+ (nullable NSDictionary<NSString*, id>*)configurationAtPath:(nullable NSString*)path
                                                       error:(NSError**)error;
- (nullable NSArray<BeamDesktopEvent*>*)eventsAndReturnError:(NSError**)error;
- (void)shutdown;
- (nullable NSArray<NSString*>*)pairingCommand:(NSString*)command
                                     arguments:(NSArray<NSString*>*)arguments
                                         error:(NSError**)error
    NS_SWIFT_NAME(pairing(command:arguments:));
+ (nullable NSString*)selectedFileForURLs:(NSArray<NSURL*>*)urls
                                    error:(NSError**)error NS_SWIFT_NAME(selectedFile(urls:));
+ (BOOL)validateContent:(NSString*)content
                   kind:(BeamContentKind)kind
                  error:(NSError**)error NS_SWIFT_NAME(validate(content:kind:));
@end

NS_ASSUME_NONNULL_END
