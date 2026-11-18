#include "Scanner.h"

struct ScannerServiceImpl : cli::Scanner::Interface
{
    ScannerServiceImpl(CommonOptions const& optsIn) { scanner = ::Scanner::Create(optsIn); }

    int32_t Scan(std::vector<shared_string> const& paths) override
    {
        for (auto const& path : paths) { scanner->ScanPath(path.str()); }
        return -1;
    }

    std::unique_ptr<::Scanner> scanner;
};

std::unique_ptr<cli::Scanner::Interface> InterfaceDelegate<cli::Scanner>::Create(CommonOptions const& opts)
{
    auto obj = std::make_unique<ScannerServiceImpl>(opts);
    return obj;
}
