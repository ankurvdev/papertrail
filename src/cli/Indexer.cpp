#include "Indexer.h"
#include "cli.h"

#include <mutex>
#include <xapian.h>

#include <filesystem>
#include <memory>

struct IndexerImpl : Indexer
{
    IndexerImpl(CommonOptions const& optsIn) : Indexer(optsIn), xapianDb(opts.workDir / "xapian_index.db", Xapian::DB_CREATE_OR_OPEN)
    {
        termGenerator.set_stemmer(Xapian::Stem("en"));
    }

    [[nodiscard]] std::string_view WorkName() const override { return "xapian-index"; }

    void Process(cli::Item& item) override;

    IndexerImpl()           = default;
    ~IndexerImpl() override = default;
    CLASS_DELETE_COPY_AND_MOVE(IndexerImpl);

    std::mutex               mutex;
    Xapian::WritableDatabase xapianDb;
    Xapian::TermGenerator    termGenerator;
};

void IndexerImpl::Process(cli::Item& item)
{
    if (item.hash.empty()) { return; }

    auto fpath = opts.workDir / ".." / "scanned" / std::filesystem::path(item.hash.str());

    if (!std::filesystem::is_directory(fpath)) { throw std::runtime_error("Expected directory"); }
    auto textContent = fpath / "index.txt";
    //auto bboxes      = fpath / "bboxes.craft_rcnn.txt";
    auto file0       = fpath / "file_0";
    auto hash        = fpath.filename().string();

    if (hash.length() != 64) { throw std::runtime_error(fmt::format("Expected a shasum has as directory. Found: {}", fpath)); }

    if (!std::filesystem::exists(textContent)) { return; }
    //if (!std::filesystem::exists(bboxes)) { return; }
    if (!std::filesystem::exists(file0)) { return; }

    std::ifstream ifs(textContent);
    std::string   content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));

    auto filename = std::filesystem::read_symlink(file0).filename();
    auto hasDoc   = [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        Xapian::PostingIterator     itBegin = xapianDb.postlist_begin(hash);
        Xapian::PostingIterator     itEnd   = xapianDb.postlist_end(hash);
        return itBegin != itEnd;
    }();

    if (hasDoc) { return; }

    {
        std::lock_guard<std::mutex> lock(mutex);
        // HELP: https://github.com/casouri/xeft/blob/master/xapian-lite.cc
        // https://github.com/RetroShare/libretroshare/blob/master/src/deep_search/filesindex.cpp
        // https://github.com/RetroShare/libretroshare/blob/master/src/deep_search/forumsindex.cpp
        // https://github.com/abo-abo/mu/blob/master/lib/mu-store-write.cc
        // https://github.com/nodegoat/nodegoat/blob/master/PROGRAMS/information_retrieval/information_retrieval_index.hpp
        // https://github.com/aarshkshah1992/xapian-docsprint/blob/master/code/c%2B%2B/index1.cc
        // https://github.com/kiwix/libkiwix/blob/main/src/library.cpp
        Xapian::Document doc;
        termGenerator.set_document(doc);

        // Add the content to the document
        termGenerator.index_text(content, 1, "C");    // Index content with prefix "C"
        termGenerator.index_text(filename, 1, "T");
        termGenerator.index_text(content);    // Index content without prefix for general search
        doc.set_data(content);
        doc.add_value(0, fpath.string());
        doc.add_boolean_term(hash);
        // Add the document to the database
        xapianDb.replace_document(hash, doc);
        xapianDb.commit();
    }
}

std::unique_ptr<Indexer> Indexer::Create(CommonOptions const& opts)
{
    return std::make_unique<IndexerImpl>(opts);
}
