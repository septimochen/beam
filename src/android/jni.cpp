#include "session.hpp"
#include <array>
#include <jni.h>

namespace {
using beam::android::Session;
std::string bytes(JNIEnv* env, jbyteArray value) {
    if (!value)
        throw std::invalid_argument("Missing Android argument");
    const auto size = env->GetArrayLength(value);
    if (size > static_cast<jsize>(beam::max_text_size))
        throw std::invalid_argument("Android argument exceeds bounds");
    std::string result(static_cast<std::size_t>(size), '\0');
    env->GetByteArrayRegion(value, 0, size, reinterpret_cast<jbyte*>(result.data()));
    if (env->ExceptionCheck())
        throw std::runtime_error("Read Android argument failed");
    return result;
}
void fail(JNIEnv* env, const char* message) noexcept {
    if (!env->ExceptionCheck()) {
        // Exceptions use an ASCII description. User content crosses JNI only as
        // UTF-8 byte arrays, never JNI's incompatible modified UTF-8 strings.
        const auto type = env->FindClass("java/lang/IllegalStateException");
        if (type) {
            std::array<char, 1024> ascii{};
            for (std::size_t i = 0; i + 1 < ascii.size() && message[i]; ++i)
                ascii[i] = static_cast<unsigned char>(message[i]) >= 128 ? '?' : message[i];
            env->ThrowNew(type, ascii.data());
            env->DeleteLocalRef(type);
        }
    }
}
Session& session(jlong handle) {
    if (!handle)
        throw std::logic_error("Android session is closed");
    return *reinterpret_cast<Session*>(static_cast<std::uintptr_t>(handle));
}
} // namespace
extern "C" JNIEXPORT jlong JNICALL Java_org_beam_android_Native_create(JNIEnv* env, jobject,
                                                                       jbyteArray directory,
                                                                       jint port, jint timeout) {
    try {
        if (port < 1 || port > 65535 || timeout < 1 || timeout > 86400000)
            throw std::invalid_argument("Invalid Android port or timeout");
        return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(
            new Session(bytes(env, directory), static_cast<std::uint16_t>(port),
                        std::chrono::milliseconds(timeout))));
    } catch (const std::exception& error) {
        fail(env, error.what());
    } catch (...) {
        fail(env, "Create native session failed");
    }
    return 0;
}
extern "C" JNIEXPORT void JNICALL Java_org_beam_android_Native_destroy(JNIEnv*, jobject,
                                                                       jlong handle) {
    delete reinterpret_cast<Session*>(static_cast<std::uintptr_t>(handle));
}
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_beam_android_Native_call(
    JNIEnv* env, jobject, jlong handle, jint command, jobjectArray arguments) {
    try {
        if (!arguments || env->GetArrayLength(arguments) > 4)
            throw std::invalid_argument("Invalid Android argument count");
        std::vector<std::string> input;
        for (jsize i = 0; i < env->GetArrayLength(arguments); ++i) {
            auto value = static_cast<jbyteArray>(env->GetObjectArrayElement(arguments, i));
            try {
                input.push_back(bytes(env, value));
            } catch (...) {
                env->DeleteLocalRef(value);
                throw;
            }
            env->DeleteLocalRef(value);
        }
        const auto output =
            session(handle).call(static_cast<beam::android::Command>(command), input);
        const auto type = env->FindClass("[B");
        if (!type)
            return nullptr;
        const auto result = env->NewObjectArray(static_cast<jsize>(output.size()), type, nullptr);
        env->DeleteLocalRef(type);
        if (!result)
            return nullptr;
        for (std::size_t i = 0; i < output.size(); ++i) {
            const auto size = static_cast<jsize>(output[i].size());
            const auto value = env->NewByteArray(size);
            if (!value)
                return nullptr;
            env->SetByteArrayRegion(value, 0, size,
                                    reinterpret_cast<const jbyte*>(output[i].data()));
            env->SetObjectArrayElement(result, static_cast<jsize>(i), value);
            env->DeleteLocalRef(value);
            if (env->ExceptionCheck())
                return nullptr;
        }
        return result;
    } catch (const std::exception& error) {
        fail(env, error.what());
    } catch (...) {
        fail(env, "Native operation failed");
    }
    return nullptr;
}
