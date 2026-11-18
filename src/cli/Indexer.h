#pragma once

#include "ParallelWorker.h"
#include "cli.h"

#include <filesystem>
#include <utility>

struct Indexer : ParallelWorker<cli::Item>
{
    Indexer(CommonOptions optsIn) : opts(std::move(optsIn))
    {    //
        opts.workDir = opts.workDir / "indexer";
        std::filesystem::create_directories(opts.workDir);
    }

    [[nodiscard]] std::string_view WorkName() const override { return "xapian-index"; }
    [[nodiscard]] std::string WorkDesc(cli::Item const& item) const override { return fmt::format("{}", Stencil::Json::Stringify(item)); }

    virtual bool Filter(cli::Item& /* item */) { return true; }

    Indexer()           = default;
    ~Indexer() override = default;
    CLASS_DELETE_COPY_AND_MOVE(Indexer);

    // virtual void Process(std::filesystem::path& fpath) override;

    static std::unique_ptr<Indexer> Create(CommonOptions const& opts);

    CommonOptions opts;
};
