
#include "utils.h"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <fmt/std.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(__linux__)
#include <pwd.h>
#endif

std::string Sha256HashOfFile(std::filesystem::path const& filePath)
{
    std::unique_ptr<EVP_MD_CTX, void (*)(EVP_MD_CTX*)> evpCtx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_DigestInit_ex(evpCtx.get(), EVP_sha256(), nullptr);

    std::array<char, 8192> buffer = {};

    std::ifstream fp(filePath, std::ios::binary);
    if (!fp.is_open()) { throw std::runtime_error(fmt::format("Unable to open : {}", filePath)); }
    while (fp.good())
    {
        fp.read(buffer.data(), buffer.size());
        EVP_DigestUpdate(evpCtx.get(), buffer.data(), static_cast<size_t>(fp.gcount()));
    }
    fp.close();

    std::array<uint8_t, 32> hash{};

    unsigned int lengthOfHash = 0;

    if (EVP_DigestFinal_ex(evpCtx.get(), hash.data(), &lengthOfHash) == 0) { throw std::runtime_error("Error calculating has"); }

    std::stringstream ss;
    for (auto ch : hash) { fmt::print(ss, "{:02x}", ch); }
    return ss.str();
}

# if defined FOR_REFERENCE_ONLY
static std::filesystem::path GetHomeDirectory()
{
    if (auto* env = std::getenv("HOME"); env != nullptr)
    {
        auto path = std::filesystem::path(env) / ".cache" / "papertrail";
        std::filesystem::create_directories(path);
        return path;
    }
#if defined(__linux__)
    /* no $HOME is defined, fallback to getpwuid */
    struct passwd* pw = getpwuid(getuid());
    if ((pw == nullptr) || (pw->pw_dir == nullptr)) { throw std::runtime_error("Failed to find $HOME directory"); }
    return {pw->pw_dir};
#else  /* defined(__linux__) */
    throw std::runtime_error("Failed to find $HOME directory");
#endif /* defined(__linux__) */
}

std::filesystem::path GetCacheDirectory()
{
    if (auto* env = std::getenv("PAPERTRAIL_CACHE"); env != nullptr)
    {
        auto path = std::filesystem::path(env);
        if (!path.is_absolute())
        {
            throw std::runtime_error(fmt::format("PAPERTRAIL_CACHE environment variable ({}) must be an absolute path", env));
        }
        std::filesystem::create_directories(path);
        return path;
    }

#if defined(__linux__) || defined(__FreeBSD__) || defined(_AIX) || defined(__OpenBSD__)
    if (auto* env = std::getenv("XDG_CACHE_HOME"); env != nullptr)
    {
        auto path = std::filesystem::path(env) / "papertrail";
        std::filesystem::create_directories(path);
        return path;
    }
#endif

#if defined(__linux__)
    auto path = GetHomeDirectory()/ ".cache" / "papertrail";
    std::filesystem::create_directories(path);
    return path;
#elif defined(__APPLE__)
    auto path =  GetHomeDirectory()/ "Library" / "Caches" / "papertrail";
    std::filesystem::create_directories(path);
    return path;
#elif defined(_WIN32)
    auto path = std::filesystem::path(std::getenv("LOCALAPPDATA")) / "papertrail";
    std::filesystem::create_directories(path);
    return path;
#elif defined(__EMSCRIPTEN__)
    GGML_ABORT("not implemented on this platform");
#else
#error Unknown architecture
#endif
}
#endif
