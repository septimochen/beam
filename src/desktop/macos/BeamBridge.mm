#import "BeamBridge.h"
#include "beam/beam.hpp"
#include "beam/config.hpp"
#include "beam/identity.hpp"
#include "desktop/service.hpp"
#ifdef BEAM_HAS_PAIRING
#include "beam/pairing.hpp"
#endif
#include <csignal>
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
#ifdef BEAM_HAS_PAIRING
    std::unique_ptr<beam::PairingSession> _pairing;
#endif
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
            // MsQuic's Darwin UDP sends can raise process-wide SIGPIPE on failure.
            // The app owns this policy; let transport errors reach the UI instead.
            if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
                throw std::runtime_error("Configure network signal handling failed");
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
- (NSArray<NSString*>*)pairingCommand:(NSString*)command
                            arguments:(NSArray<NSString*>*)arguments
                                error:(NSError**)error {
    try {
#ifdef BEAM_HAS_PAIRING
        auto& service = [self service];
        const auto action = utf8(command);
        const auto argument = [&](NSUInteger index) {
            if (index >= arguments.count || [arguments[index] length] > 2048)
                throw std::invalid_argument("Invalid pairing argument");
            return utf8(arguments[index]);
        };
        if (action == "host") {
            if (service.is_sending() || service.is_checking())
                throw std::logic_error("Wait for the active transfer before pairing");
            _pairing = std::make_unique<beam::PairingSession>(
                beam::PairingOptions{_config.state_directory, argument(0), _config.port});
            _pairing->host();
        } else {
            if (!_pairing) {
                if (action == "cancel")
                    return @[];
                throw std::logic_error("Open Add device first");
            }
            if (action == "join")
                _pairing->join(argument(0));
            else if (action == "pin")
                _pairing->join_pin(argument(0), argument(1));
            else if (action == "approve") {
                if (service.is_sending() || service.is_checking())
                    throw std::logic_error("Wait for the active transfer before pairing");
                service.stop_receiving();
                _pairing->approve(argument(0));
            } else if (action == "browse")
                _pairing->browse();
            else if (action == "reject")
                _pairing->reject();
            else if (action == "cancel")
                _pairing->cancel();
            else if (action == "forget")
                _pairing->forget_incomplete();
            else if (action != "snapshot")
                throw std::invalid_argument("Unknown pairing action");
        }
        const auto snapshot = _pairing->snapshot();
        std::string rows;
        for (const auto& row : snapshot.qr)
            rows += row + "\n";
        NSMutableArray<NSString*>* values = [NSMutableArray arrayWithArray:@[
            display(beam::pairing_state_name(snapshot.state)), display(snapshot.message),
            display(snapshot.invitation), display(snapshot.pin), display(snapshot.remote_name),
            display(snapshot.alias), display(std::to_string(snapshot.port)), display(rows)
        ]];
        for (const auto& device : snapshot.nearby) {
            [values addObject:display(device.name)];
            [values addObject:display(device.endpoint)];
        }
        return values;
#else
        throw std::runtime_error("Network pairing is disabled in this build");
#endif
    } catch (const std::exception& exception) {
        report(error, exception);
        return nil;
    }
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
- (NSArray<NSString*>*)deviceCommand:(NSString*)command
                           arguments:(NSArray<NSString*>*)arguments
                               error:(NSError**)error {
    try {
        auto& service = [self service];
        const auto action = utf8(command);
        const auto expected =
            action == "edit" ? 7U
            : action == "save" || action == "read" || action == "preview" || action == "import"
                ? 1U
                : 0U;
        if (arguments.count != expected)
            throw std::invalid_argument("Invalid device command arguments");
        const auto argument = [&](NSUInteger i) {
            if (arguments[i].length > beam::max_device_backup_size)
                throw std::invalid_argument("Device argument too large");
            auto result = utf8(arguments[i]);
            if (result.size() > beam::max_device_backup_size ||
                result.find('\0') != std::string::npos)
                throw std::invalid_argument("Invalid device argument");
            return result;
        };
        if (action == "details" || action == "identity" || action == "save" ||
            action == "preview" || action == "import") {
            beam::IdentityStore store(_config.state_directory, true);
            store.initialize();
        }
        NSMutableArray<NSString*>* values = [NSMutableArray array];
        if (action == "details") {
            for (const auto& peer : beam::IdentityStore(_config.state_directory).peers())
                [values addObjectsFromArray:@[
                    display(peer.name), display(peer.endpoint), display(peer.tailscale_endpoint),
                    display(peer.identity.fingerprint), display(peer.identity.certificate_pem)
                ]];
        } else if (action == "edit") {
#ifdef BEAM_HAS_PAIRING
            if (_pairing)
                _pairing->cancel();
#endif
            service.edit_peer(argument(0), {argument(1), argument(2), argument(3), argument(4),
                                            argument(5), argument(6)});
        } else if (action == "save") {
            beam::write_device_backup(
                argument(0), beam::IdentityStore(_config.state_directory).export_devices());
        } else if (action == "read") {
            [values addObject:display(beam::read_device_backup(argument(0)))];
        } else if (action == "identity") {
            const auto identity = beam::IdentityStore(_config.state_directory).identity();
            [values addObjectsFromArray:@[
                display(identity.fingerprint), display(identity.tls_name),
                display(identity.certificate_pem)
            ]];
        } else if (action == "preview" || action == "import") {
            const auto result =
                action == "preview"
                    ? beam::IdentityStore(_config.state_directory).preview_import(argument(0))
                    : service.import_devices(argument(0));
            [values addObjectsFromArray:@[
                display(result.source_fingerprint), display(std::to_string(result.added)),
                display(std::to_string(result.existing)),
                result.source_fingerprint ==
                        beam::IdentityStore(_config.state_directory).identity().fingerprint
                    ? @"1"
                    : @"0"
            ]];
            for (const auto& peer : result.peers)
                [values addObjectsFromArray:@[
                    display(peer.name), display(peer.endpoint), display(peer.tailscale_endpoint),
                    display(peer.identity.fingerprint)
                ]];
        } else
            throw std::invalid_argument("Unknown device command");
        return values;
    } catch (const std::exception& exception) {
        report(error, exception);
        return nil;
    }
}
- (BOOL)unpairPeer:(NSString*)peer error:(NSError**)error {
    try {
        [self service].unpair(utf8(peer));
        return YES;
    } catch (const std::exception& exception) {
        report(error, exception);
        return NO;
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
#ifdef BEAM_HAS_PAIRING
    _pairing.reset();
#endif
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
