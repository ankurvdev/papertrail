#include "CommonMacros.h"

#include "cli.h"
#include "webservice_boostbeast.h"

#include <EmbeddedResource.h>
#include <stencil/protocol_cli.h>
#include <stencil/timestamped.h>
#include <xapian.h>

SUPPRESS_WARNINGS_START
SUPPRESS_STL_WARNINGS
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#include <array>
#include <string_view>

SUPPRESS_WARNINGS_END

using time_point = std::chrono::time_point<std::chrono::system_clock>;

DECLARE_RESOURCE_COLLECTION(web);
DECLARE_RESOURCE(web, index_html);

using namespace cli;

struct ServiceImpl;

// static auto MimeType(boost::beast::string_view path)
//{
//     return stwsvc::MimeType(path);
// }
struct SearcherServiceImpl : cli::Searcher::Interface
{
    cli::SearchArgs args;
    CommonOptions   opts;

    bool printToConsole{false};
    int  currentRequest{0};

    [[nodiscard]] cli::SearchArgs const& Args() const;

    bool Filter(cli::SearchResultItem& /*item*/) { return true; }    // NOLINT

    // Inherited via Catalog
    std::vector<cli::SearchResultItem> Search(shared_string const& queryStr) override
    {
        using namespace std::string_view_literals;

        if (queryStr.length() == 0) { return {}; }

        static constexpr auto StopWords
            = std::to_array<std::string_view>({"a",    "about", "an",    "and",   "are",  "as",  "at",     //
                                               "be",   "by",    "en",    "for",   "from", "how",           //
                                               "i",    "in",    "is",    "it",    "of",                    //
                                               "on",   "or",    "that",  "the",   "this", "to",  "was",    //
                                               "what", "when",  "where", "which", "who",  "why", "will", "with"});

        Xapian::SimpleStopper mystopper(std::begin(StopWords), std::end(StopWords));
        Xapian::Stem          stemmer("english");
        Xapian::Database      db(opts.workDir / "indexer" / "xapian_index.db");
        Xapian::QueryParser   parser;

        parser.set_database(db);
        parser.set_stemmer(stemmer);
        parser.set_default_op(Xapian::Query::OP_OR);
        parser.set_stemming_strategy(Xapian::QueryParser::STEM_SOME);
        parser.set_stopper(&mystopper);

        auto queryNoPrefix = parser.parse_query(queryStr.str(),
                                                Xapian::QueryParser::FLAG_DEFAULT | Xapian::QueryParser::FLAG_SPELLING_CORRECTION
                                                    | Xapian::QueryParser::FLAG_PARTIAL);
        auto queryDefaultPrefix
            = parser.parse_query(queryStr.str(), Xapian::QueryParser::FLAG_DEFAULT | Xapian::QueryParser::FLAG_SPELLING_CORRECTION, "S");
        auto            query = Xapian::Query(Xapian::Query::OP_OR, queryNoPrefix, queryDefaultPrefix);
        Xapian::Enquire enquire(db);
        enquire.set_query(query);
        {
            Xapian::BM25Weight weight(1 /* k1 */, 0 /* k2 */, 1 /* k3 */, 0.5 /* b */, 0.5 /* min_normlen */);    // NOLINT
            enquire.set_weighting_scheme(weight);
        }

        Xapian::MSet mset = enquire.get_mset(Xapian::doccount(0), Xapian::doccount(10) /* msize */);

        // auto lower_bound = mset.get_matches_lower_bound();
        // auto estimate    = mset.get_matches_estimated();
        // auto upper_bound = mset.get_matches_upper_bound();
        //  if (lower_bound == upper_bound) { cout << "Exactly " << estimate << " matches\n"; }
        //  else { cout << "Between " << lower_bound << " and " << upper_bound << " matches, best estimate is " << estimate << '\n'; }

        // cout << "MSet:\n";
        std::vector<cli::SearchResultItem> results;
        for (auto i = mset.begin(); i != mset.end(); ++i)
        {
            auto doc = i.get_document();
            // auto data  = doc.get_data();
            auto symlinkdir   = std::filesystem::absolute(std::filesystem::path(doc.get_value(0)));
            auto relativePath = std::filesystem::relative(symlinkdir, opts.workDir);

            auto file0 = symlinkdir / "file_0";
            if (!std::filesystem::exists(file0)) { continue; }

            auto realpath = std::filesystem::read_symlink(file0);
            if (!std::filesystem::exists(realpath)) { continue; }
            auto ftime = std::chrono::file_clock::to_sys(std::filesystem::last_write_time(realpath));

            cli::SearchResultItem item{};

            item.hash         = shared_string(symlinkdir.filename().string());
            item.path         = shared_string(realpath.string());
            item.size         = std::filesystem::file_size(realpath);
            item.lastmodified = std::chrono::time_point_cast<Stencil::Timestamp::duration>(ftime);
            item.content      = shared_string{"content"};
            item.snippet      = shared_string{"snippet"};
            if (relativePath.is_relative() && relativePath.begin() != relativePath.end() && *relativePath.begin() == "scanned")
            {
                item.url = shared_string{fmt::format("/files/{}/file_0", relativePath)};
            }
            item.filename = shared_string{realpath.filename().string()};
            results.push_back(item);
            // fmt::print("{}: [{}]:{}\n", *i, i.get_weight(), fpath);
        }
        return results;
    }
};

std::unique_ptr<cli::Searcher::Interface> InterfaceDelegate<cli::Searcher>::Create(CommonOptions const& opts)
{
    auto obj  = std::make_unique<SearcherServiceImpl>();
    obj->opts = opts;
    return obj;
}
