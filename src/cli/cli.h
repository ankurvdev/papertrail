#pragma once
#include "cli.pidl.h"

template <typename T> struct InterfaceDelegate;

struct CommonOptions
{
    std::filesystem::path workDir;

    bool console = false;
    bool verbose = false;
    bool debug   = false;
};

template <> struct InterfaceDelegate<cli::Searcher>
{
    static std::unique_ptr<cli::Searcher::Interface> Create(CommonOptions const& opts);
};
template <> struct InterfaceDelegate<cli::Scanner>
{
    static std::unique_ptr<cli::Scanner::Interface> Create(CommonOptions const& opts);
};

void SearcherCli(CommonOptions const& opts, cli::SearchArgs const& args);
void ScannerCli(CommonOptions const& opts, cli::ScannerArgs const& args);
