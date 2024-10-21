#pragma once

#include "ParallelWorker.h"

#include <cli.pidl.h>

template <> struct fmt::formatter<std::shared_ptr<cli::Item>> : fmt::formatter<std::string_view>
{

    // Formats the point p using the parsed format specification (presentation)
    // stored in this formatter.
    template <typename FormatContext> auto format(std::shared_ptr<cli::Item> const& item, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "{}", Stencil::Json::Stringify(*item));
    }
};

struct Scanner : ParallelWorker<std::filesystem::path>
{
    Scanner(cli::SearchArgs const& args);
    std::string_view work_name() const override { return "ocr-scan"; }

    /*     struct MissingM3UHeader : public std::runtime_error
        {
            MissingM3UHeader(std::filesystem::path const& _fpath) : std::runtime_error("Missing M3u Header"), fpath(_fpath) {}
            std::filesystem::path fpath;
        };
     */

    void ScanDirectory(std::filesystem::path const& dirpath)
    {
        for (const auto& entry : std::filesystem::directory_iterator(dirpath))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".m3u") { add_item(std::filesystem::path{entry.path()}); }
        }
    }

    virtual bool filter(cli::Item& /* item */) { return true; }

    Scanner()                   = default;
    virtual ~Scanner() override = default;
    CLASS_DELETE_COPY_AND_MOVE(Scanner);

    virtual void process(std::filesystem::path& fpath) override;

    std::filesystem::path _workDir;
};
