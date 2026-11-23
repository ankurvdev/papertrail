#pragma once
#include "CommonMacros.h"

#include <boost/url.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>
#include <fmt/format.h>
#include <fmt/std.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <string>

struct FileDownloadErrorAccessDenied : std::exception
{
    FileDownloadErrorAccessDenied(long codeIn) : code(codeIn) {}
    FileDownloadErrorAccessDenied() = default;

    long code{};

    [[nodiscard]] char const* what() const noexcept override { return "FileDownloadError_AccessDenied"; }
};

struct FileDownloadErrorHttpError : std::exception
{
    FileDownloadErrorHttpError(long codeIn) : code(codeIn) {}
    FileDownloadErrorHttpError() = default;

    long code{};

    [[nodiscard]] char const* what() const noexcept override { return "FileDownloadError_HttpError"; }
};
struct FileDownloadErrorTooBig : std::exception
{
    static constexpr size_t MaxSize = static_cast<size_t>(128u * 1024u * 1024u);    // 128 MB

    FileDownloadErrorTooBig(size_t sizeIn) : size(sizeIn) {}
    FileDownloadErrorTooBig() = default;

    size_t size{};

    [[nodiscard]] char const* what() const noexcept override { return "FileDownloadError_TooBig"; }
};

struct FileDownloadErrorUrlResolveError : std::exception
{
    std::string message;

    FileDownloadErrorUrlResolveError(std::string&& what) : message(std::move(what)) {}
    [[nodiscard]] char const* what() const noexcept LFTBND override { return message.c_str(); }
};

struct FileDownloadErrorLogicError : std::exception
{
    std::string message;

    FileDownloadErrorLogicError(std::string&& what) : message(std::move(what)) {}
    [[nodiscard]] char const* what() const noexcept LFTBND override { return message.c_str(); }
};

std::filesystem::path DownloadFile(std::string const& urlstr, std::filesystem::path const& fpathIn = {});
std::string
DownloadContent(std::string_view const& urlstr, std::filesystem::path const& cachePathIn, std::chrono::seconds const& cacheMaxAgeInSec);

inline void CheckResponseCode(long responseCode)
{
    if (responseCode != 200)    // NOLINT
    {
        fmt::print(stderr, "Response Code: {}\n", responseCode);
        if (responseCode == 404 || responseCode == 403 || responseCode == 423 || responseCode == 425)    // NOLINT
        {
            throw FileDownloadErrorAccessDenied(responseCode);
        }
        throw FileDownloadErrorHttpError(responseCode);
        // Close the file stream
    }
}
inline std::filesystem::path DownloadFile(std::string const& urlstr, std::filesystem::path const& fpathIn)
{
    auto fpath = fpathIn;
    if (fpath.empty() || (std::filesystem::exists(fpath) && std::filesystem::is_directory(fpath)))
    {
        auto urlp = boost::urls::parse_uri(urlstr);
        if (!urlp) { throw std::runtime_error(fmt::format("boost::urls::parse_uri error : {}", urlp.error().to_string())); }
        fpath = fpath / std::filesystem::path(urlp->path()).filename();
    }

    auto fpathtmp = fpath;
    fpathtmp += fmt::format(".{}.tmp", std::this_thread::get_id());
    std::error_code ec;
    std::filesystem::remove(fpathtmp, ec);
    std::unique_ptr<std::ofstream> ofs;

    try
    {
        // Initialize curlpp
        curlpp::Cleanup cleaner;
        curlpp::Easy    request;
        // URL of the file to download
        request.setOpt<curlpp::options::Url>(urlstr);
        size_t totalBytes = 0;
        // Open a file stream to save the downloaded file
        curlpp::options::WriteFunction writeFunction([&](char* ptr, size_t size, size_t nmemb) {
            if (!ofs) { ofs = std::make_unique<std::ofstream>(fpathtmp, std::ios::binary); }
            auto bytes = size * nmemb;
            totalBytes += bytes;
            if (totalBytes > FileDownloadErrorTooBig::MaxSize)
            {
                auto msg = fmt::format("File Size Too Big Abandoning {} at {}", urlstr, totalBytes);
                fmt::print(stderr, "{}\n", msg);
                throw FileDownloadErrorTooBig(totalBytes);
            }
            ofs->write(ptr, static_cast<std::streamsize>(bytes));
            return bytes;
        });

        request.setOpt(writeFunction);
        //        request.setOpt(curlpp::options::Verbose(true));
        request.setOpt(curlpp::options::UserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)"));
        request.setOpt(curlpp::options::FollowLocation(true));
        request.setOpt(curlpp::options::SslVerifyHost(0));
        request.setOpt(curlpp::options::SslVerifyPeer(false));
        request.setOpt(curlpp::options::Timeout(1024));    // Sustained
        // Perform the request
        request.perform();

        CheckResponseCode(curlpp::infos::ResponseCode::get(request));

        if (ofs) { ofs->close(); }

        if (!ofs) { throw std::runtime_error("Zero bytes downloaded"); }
        std::filesystem::rename(fpathtmp, fpath);
    } catch (curlpp::LogicError const& ex)
    {
        std::filesystem::remove(fpathtmp, ec);
        throw FileDownloadErrorLogicError{ex.what()};
    } catch (curlpp::LibcurlRuntimeError const& ex)
    {
        std::filesystem::remove(fpathtmp, ec);
        if (ex.whatCode() == CURLE_COULDNT_RESOLVE_HOST) { throw FileDownloadErrorUrlResolveError{ex.what()}; }
        throw;
    } catch (std::exception const&)
    {
        std::filesystem::remove(fpathtmp, ec);
        throw;
    }
    return fpath;
}

std::string
DownloadContent(std::string_view const& urlstr, std::filesystem::path const& cachePathIn, std::chrono::seconds const& cacheMaxAgeInSec)
{
    auto fpath = cachePathIn;
    if (!fpath.empty() && std::filesystem::exists(fpath) && std::filesystem::is_directory(fpath))
    {
        auto urlp = boost::urls::parse_uri(urlstr);
        if (!urlp) { throw std::runtime_error(fmt::format("boost::urls::parse_uri error : {}", urlp.error().to_string())); }
        fpath = fpath / std::filesystem::path(urlp->path()).filename();
    }

    auto fpathtmp = fpath;
    fpathtmp += fmt::format(".{}.tmp", std::this_thread::get_id());
    std::error_code ec;
    std::filesystem::remove(fpathtmp, ec);
    std::unique_ptr<std::ofstream> ofs;
    std::filesystem::remove(fpathtmp, ec);

    bool writeCache = !fpath.empty();

    if (writeCache && std::filesystem::exists(fpath) && cacheMaxAgeInSec != std::chrono::seconds{0})
    {
        auto ftime = std::filesystem::last_write_time(fpath, ec);
        if (!ec)
        {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(std::filesystem::file_time_type::clock::now() - ftime);
            if (age < cacheMaxAgeInSec)
            {
                // Cache valid
                return {std::istreambuf_iterator<char>(std::ifstream(fpath).rdbuf()), std::istreambuf_iterator<char>()};
            }
        }
        std::filesystem::remove(fpathtmp, ec);
    }

    try
    {
        std::stringstream ss;

        // Initialize curlpp
        curlpp::Cleanup cleaner;
        curlpp::Easy    request;
        // URL of the file to download
        request.setOpt<curlpp::options::Url>(std::string(urlstr));
        size_t totalBytes = 0;
        // Open a file stream to save the downloaded file
        curlpp::options::WriteFunction writeFunction([&](char* ptr, size_t size, size_t nmemb) {
            if (writeCache && !ofs) { ofs = std::make_unique<std::ofstream>(fpathtmp, std::ios::binary); }

            auto bytes = size * nmemb;
            totalBytes += bytes;
            if (totalBytes > FileDownloadErrorTooBig::MaxSize)
            {
                auto msg = fmt::format("File Size Too Big Abandoning {} at {}", urlstr, totalBytes);
                fmt::print(stderr, "{}\n", msg);
                throw FileDownloadErrorTooBig(totalBytes);
            }
            ss.write(ptr, static_cast<std::streamsize>(bytes));
            if (writeCache) { ofs->write(ptr, static_cast<std::streamsize>(bytes)); }
            return bytes;
        });

        request.setOpt(writeFunction);
        //        request.setOpt(curlpp::options::Verbose(true));
        request.setOpt(curlpp::options::UserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)"));
        request.setOpt(curlpp::options::FollowLocation(true));
        request.setOpt(curlpp::options::SslVerifyHost(0));
        request.setOpt(curlpp::options::SslVerifyPeer(false));
        request.setOpt(curlpp::options::Timeout(1024));    // Sustained
        // Perform the request
        request.perform();
        if (ofs) { ofs->close(); }

        CheckResponseCode(curlpp::infos::ResponseCode::get(request));

        auto str = ss.str();
        if (str.empty()) { throw std::runtime_error("Zero bytes downloaded"); }
        if (writeCache) { std::filesystem::rename(fpathtmp, fpath); }

        return str;
    } catch (curlpp::LogicError const& ex)
    {
        std::filesystem::remove(fpathtmp, ec);
        throw FileDownloadErrorLogicError{ex.what()};
    } catch (curlpp::LibcurlRuntimeError const& ex)
    {
        std::filesystem::remove(fpathtmp, ec);
        if (ex.whatCode() == CURLE_COULDNT_RESOLVE_HOST) { throw FileDownloadErrorUrlResolveError{ex.what()}; }
        throw;
    } catch (std::exception const&)
    {
        std::filesystem::remove(fpathtmp, ec);
        throw;
    }
}
