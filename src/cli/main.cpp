#include "SetThreadName.h"

#include "Scanner.h"
#include "cli.h"
#include "webservice_boostbeast.h"

#include "EmbeddedResource.h"
#include <memory>
#include <stencil/interfaces.h>

DECLARE_RESOURCE_COLLECTION(web);
DECLARE_RESOURCE(web, index_html);
DECLARE_RESOURCE(web, favicon_png);

namespace stwsvc = Stencil::websvc;

struct WebServiceImpl;
template <> struct Stencil::InterfaceSvcTraits<WebServiceImpl, cli::Searcher>
{
    static constexpr bool            NeedsSessions() { return true; }
    static cli::Searcher::Interface& QueryInterface(WebServiceImpl& impl LFTBND);
};

struct WebServiceImpl : stwsvc::WebServiceT<WebServiceImpl, cli::Searcher>
{
    WebServiceImpl(CommonOptions optsIn) : opts(std::move(optsIn))
    {
        searcher = InterfaceDelegate<cli::Searcher>::Create(opts);
        scanner  = InterfaceDelegate<cli::Scanner>::Create(opts);
    }
    ~WebServiceImpl() = default;
    CLASS_DELETE_COPY_AND_MOVE(WebServiceImpl);

    void Start()
    {
        // for (auto scan_dir : args().scan) { _m3uparser.scan_directory(scan_dir.str()); }
        // for (auto file : args().file) { _m3uparser.add_item(file.str()); }
        //
        //_m3uparser.start();
    }

    void WaitForFinish()
    {
        //_m3uparser.wait_for_finish();
        //_videoprober.wait_for_finish();
    }

    bool HandleRequest(Stencil::websvc::tcp_stream& stream, Stencil::websvc::Request const& req, boost::urls::url_view const& url)
    {
        using namespace Stencil::websvc;

        auto reqpath = url.path();
        if (reqpath == "" || reqpath == "/" || reqpath == "/index.html")
        {
            stwsvc::WriteStringResponse(stream, req, MimeType(".html"), ResourceLoader(LOAD_RESOURCE(web, index_html)).string());
            return true;
        }

        for (auto const rsrc : LOAD_RESOURCE_COLLECTION(web))
        {
            auto path = "/" + std::string(rsrc.name());
            if (path != reqpath) { continue; }
            stwsvc::WriteStringResponse(stream, req, MimeType(std::filesystem::path(path).extension().string()), rsrc.string());
            return true;
        }
#if defined WEB_USE_STATIC_FILES
        if (reqpath.starts_with("/static/"))
        {
            auto fpath = std::filesystem::path(WEB_USE_STATIC_FILES) / std::filesystem::path(std::string_view(reqpath.substr(8)));
            stwsvc::WriteFileResponse(stream, req, fpath, MimeType(fpath.string()));
            return true;
        }
#endif
        if (reqpath == "/favicon.ico")
        {
            stwsvc::WriteStringResponse(stream, req, MimeType(".png"), ResourceLoader(LOAD_RESOURCE(web, favicon_png)).string());
            return true;
        }
        if (reqpath.starts_with("/files/"))
        {
            auto fpath = opts.workDir / std::filesystem::path(std::string_view(reqpath.substr(7)));
            if (std::filesystem::is_symlink(fpath)) { fpath = std::filesystem::read_symlink(fpath); }
            stwsvc::WriteFileResponse(stream, req, fpath, MimeType(fpath.string()));
            return true;
        }
        return false;
    }
    CommonOptions                             opts;
    std::unique_ptr<cli::Searcher::Interface> searcher;
    std::unique_ptr<cli::Scanner::Interface>  scanner;
};

inline cli::Searcher::Interface& Stencil::InterfaceSvcTraits<WebServiceImpl, cli::Searcher>::QueryInterface(WebServiceImpl& impl)
{ return *impl.searcher; }

int main(int argc, char const* const argv[])
try
{

    auto parseResult = Stencil::CLI::Parse<cli::CommandLineOptions>(argc - 1, &argv[1]);    // NOLINT
    if (parseResult.helpRequested || !parseResult.success)
    {
        fmt::print(stderr, "{}\n", fmt::join(Stencil::CLI::GenerateHelp(parseResult.obj), "\n"));
        return parseResult.helpRequested ? 0 : -1;
    }
    CommonOptions opts{};
    opts.workDir = std::filesystem::absolute(parseResult.obj.work_dir.empty() ? std::filesystem::current_path()
                                                                              : std::filesystem::path{parseResult.obj.work_dir.str()});
    opts.console = parseResult.obj.console;
    opts.verbose = parseResult.obj.verbose;
    opts.debug   = parseResult.obj.debug;

    WebServiceImpl svc{opts};

    if (!opts.console) { svc.StartOnPort(parseResult.obj.web.port); }

    svc.scanner->Scan(parseResult.obj.scan.path);
    for (auto& q : parseResult.obj.search.query)
    {
        auto item = svc.searcher->Search(q);
        fmt::print("Search results for query '{}':\n", q.str());
        for (auto& it : item) { fmt::print("\t{}\n", it.path.str()); }
    }

    svc.WaitForStop();

    return 0;

} catch (std::exception const& ex)
{
    fmt::print(stderr, "Exception: {}\n", ex.what());
    return -1;
}
