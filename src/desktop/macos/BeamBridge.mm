#import "BeamBridge.h"
#include "beam/beam.hpp"
#include "beam/config.hpp"
#include "beam/identity.hpp"
#include "desktop/service.hpp"
#include <memory>

namespace {
std::string utf8(NSString* value) {
    NSData* data = [value dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    if (!data)
        throw std::invalid_argument("Text is not valid UTF-8");
    if (!data.length)
        return {};
    return {static_cast<const char*>(data.bytes), data.length};
}
NSString* display(std::string_view value) {
    NSString* result = [[NSString alloc] initWithBytes:value.data()
                                                length:value.size()
                                              encoding:NSUTF8StringEncoding];
    return result ? result : @"Unsupported text encoding";
}
beam::ContentKind content_kind(BeamContentKind kind) {
    switch (kind) {
    case BeamContentKindFile:
        return beam::ContentKind::file;
    case BeamContentKindText:
        return beam::ContentKind::text;
    }
    throw std::invalid_argument("Unsupported content type");
}
std::string content_bytes(NSString* content, beam::ContentKind kind) {
    if (kind == beam::ContentKind::file)
        return {};
    const auto limit = beam::max_text_size;
    // Reject obviously oversized UTF-16 before allocating a second full copy.
    // The core still validates the exact UTF-8 byte length and syntax.
    if (content.length > limit)
        throw std::invalid_argument("Text exceeds 64 KiB");
    return utf8(content);
}
void report(NSError** output, const std::exception& error) {
    if (output) {
        const auto* transfer = dynamic_cast<const beam::TransferError*>(&error);
        *output = [NSError errorWithDomain:@"org.beam.desktop"
                                      code:transfer ? static_cast<NSInteger>(transfer->code) : -1
                                  userInfo:@{NSLocalizedDescriptionKey : display(error.what())}];
    }
}
} // namespace

@implementation BeamDesktopEvent
- (instancetype)initWithEvent:(const beam::desktop::Event&)event {
    self = [super init];
    if (self) {
        _sending = event.direction == beam::desktop::Direction::send;
        _checking = event.direction == beam::desktop::Direction::check;
        _kind = static_cast<BeamEventKind>(event.kind);
        _contentKind = static_cast<BeamContentKind>(event.result.kind);
        _stage = static_cast<BeamTransferStage>(event.progress.stage);
        _message = display(event.message);
        _filename = display(event.result.filename);
        _bytes = event.kind == beam::desktop::EventKind::progress ? event.progress.bytes
                                                                  : event.result.bytes;
        _total = event.progress.total;
        _bytesPerSecond = event.progress.bytes_per_second;
        _text = display(event.result.text);
    }
    return self;
}
@end

@implementation BeamDesktopBridge {
    beam::desktop::Config _config;
    std::unique_ptr<beam::desktop::Service> _service;
}
- (instancetype)initWithStateDirectory:(NSString*)directory
                                  port:(NSUInteger)port
                             advertise:(BOOL)advertise
                                 error:(NSError**)error {
    return [self initWithStateDirectory:directory
                                   port:port
                              advertise:advertise
                                timeout:30000
                                  error:error];
}
- (instancetype)initWithStateDirectory:(NSString*)directory
                                  port:(NSUInteger)port
                             advertise:(BOOL)advertise
                               timeout:(NSUInteger)timeout
                                 error:(NSError**)error {
    self = [super init];
    if (self) {
        try {
            if (port == 0 || port > 65535)
                throw std::invalid_argument("Listen port must be between 1 and 65535");
            _config.state_directory = directory ? std::filesystem::path(utf8(directory))
                                                : beam::default_state_directory();
            if (_config.state_directory.string().find('\0') != std::string::npos)
                throw std::invalid_argument("Invalid state directory");
            _config.port = static_cast<std::uint16_t>(port);
            _config.advertise = advertise;
            if (!timeout || timeout > 86400000)
                throw std::invalid_argument("Invalid transfer timeout");
            _config.timeout = std::chrono::milliseconds(timeout);
            _service = std::make_unique<beam::desktop::Service>(_config);
        } catch (const std::exception& exception) {
            report(error, exception);
            return nil;
        }
    }
    return self;
}
- (beam::desktop::Service&)service {
    if (!_service)
        throw std::logic_error("Beam has shut down");
    return *_service;
}
- (BOOL)sending {
    return _service && _service->is_sending();
}
- (BOOL)receiving {
    return _service && _service->is_receiving();
}
- (BOOL)checking {
    return _service && _service->is_checking();
}
- (BOOL)checkPeer:(NSString*)peer error:(NSError**)error {
    try {
        [self service].check_peer(utf8(peer));
        return YES;
    } catch (const std::exception& exception) {
        report(error, exception);
        return NO;
    }
}
+ (NSDictionary<NSString*, id>*)configurationAtPath:(NSString*)path error:(NSError**)error {
    try {
        const auto config = beam::load_config(path ? std::filesystem::path(utf8(path))
                                                   : beam::default_config_file());
        return @{
            @"output" : display(config.receive_folder.string()),
            @"port" : @(config.listen_port),
            @"advertise" : @(config.discovery),
            @"timeout" : @(config.timeout.count())
        };
    } catch (const std::exception& exception) {
        report(error, exception);
        return nil;
    }
}
- (NSString*)version {
    return display(beam::version());
}
- (NSArray<NSString*>*)peersAndReturnError:(NSError**)error {
    try {
        static_cast<void>([self service]);
        beam::IdentityStore store(_config.state_directory);
        NSMutableArray<NSString*>* peers = [NSMutableArray array];
        for (const auto& peer : store.peers())
            [peers addObject:display(peer.name)];
        return peers;
    } catch (const std::exception& exception) {
        report(error, exception);
        return nil;
    }
}
- (BOOL)sendToPeer:(NSString*)peer
              kind:(BeamContentKind)kind
              file:(NSString*)file
           content:(NSString*)content
             error:(NSError**)error {
    try {
        const auto type = content_kind(kind);
        [self service].send({utf8(peer), type, utf8(file), content_bytes(content, type)});
        return YES;
    } catch (const std::exception& exception) {
        report(error, exception);
        return NO;
    }
}
- (BOOL)startReceivingInDirectory:(NSString*)directory error:(NSError**)error {
    try {
        const auto path = utf8(directory);
        if (path.empty() || path.find('\0') != std::string::npos)
            throw std::invalid_argument("Choose a receive folder first");
        [self service].start_receiving(path);
        return YES;
    } catch (const std::exception& exception) {
        report(error, exception);
        return NO;
    }
}
- (void)cancelSend {
    if (_service)
        _service->cancel_send();
}
- (void)stopReceiving {
    if (_service)
        _service->stop_receiving();
}
- (NSArray<BeamDesktopEvent*>*)eventsAndReturnError:(NSError**)error {
    try {
        NSMutableArray<BeamDesktopEvent*>* events = [NSMutableArray array];
        for (const auto& event : [self service].take_events())
            [events addObject:[[BeamDesktopEvent alloc] initWithEvent:event]];
        return events;
    } catch (const std::exception& exception) {
        report(error, exception);
        return nil;
    }
}
- (void)shutdown {
    _service.reset();
}
+ (NSString*)selectedFileForURLs:(NSArray<NSURL*>*)urls error:(NSError**)error {
    try {
        if (urls.count != 1 || !urls.firstObject.isFileURL || urls.firstObject.host.length != 0 ||
            !urls.firstObject.path)
            throw std::invalid_argument("Choose or drop one local file at a time");
        const auto path = utf8(urls.firstObject.path);
        beam::desktop::validate_request({"selection", beam::ContentKind::file, path, {}});
        return display(path);
    } catch (const std::exception& exception) {
        report(error, exception);
        return nil;
    }
}
+ (BOOL)validateContent:(NSString*)content kind:(BeamContentKind)kind error:(NSError**)error {
    try {
        const auto type = content_kind(kind);
        if (type == beam::ContentKind::file)
            throw std::invalid_argument("Choose a file for binary content");
        beam::validate_content(type, content_bytes(content, type));
        return YES;
    } catch (const std::exception& exception) {
        report(error, exception);
        return NO;
    }
}
@end
