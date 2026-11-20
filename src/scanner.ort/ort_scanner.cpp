// ocrtest: for each input <file> writes <file>.json (blocks + lines with boxes), <file>.md (reading-order text)
// and <file>.overlay.jpg (original | red line boxes with the recognised text) into the output directory.
//
//   PP-DocLayoutV3   layout blocks + reading order   ONNX Runtime
//   PP-OCRv6 medium  text line detection + text      ONNX Runtime
//
// Pre/post-processing follows PaddleOCR 3.x (PaddleX) so results match the official Python pipeline.
// Usage: ocrtest <image> [<image> ...] <output-dir>
#include "CommonMacros.h"
#include "FileDownload.h"
#include "Scanner.h"

#include <clipper2/clipper.h>
#include <nlohmann/json.hpp>
#include <onnxruntime/onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/freetype.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <numeric>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using Quad   = std::array<cv::Point2f, 4>;    // top-left, top-right, bottom-right, bottom-left
using Json   = nlohmann::ordered_json;

namespace
{

using Quad = std::array<cv::Point2f, 4>;    // top-left, top-right, bottom-right, bottom-left

struct Line
{
    Quad        quad{};
    cv::Rect2f  box;
    std::string text;
    float       score = 0;
    int         block = -1;
};

struct Block
{
    std::string      label;
    float            score = 0;
    cv::Rect2f       box;
    std::vector<int> lines;    // indices into Page::lines, reading order
    std::string      text;
};

struct Page
{
    std::vector<Block> blocks;    // reading order
    std::vector<Line>  lines;
};

struct Model
{
    Ort::MemoryInfo          mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Session             session{nullptr};
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
};

struct Models
{
    Model                            layout;
    Model                            det;
    Model                            rec;
    std::vector<std::string>         labels;     // layout classes
    std::vector<std::string>         charset;    // [0] = CTC blank
    cv::Ptr<cv::freetype::FreeType2> font;       // empty: overlay without text
};

struct LayoutBox
{
    int        cls   = 0;
    float      score = 0;
    cv::Rect2f box;
    float      order = 0;
};

}    // namespace

// ---------------------------------------------------------------------------------------------
// Settings (PaddleOCR 3.x OCR pipeline and PaddleOCR-VL-1.6 layout defaults)
// ---------------------------------------------------------------------------------------------
static constexpr double TallRatio = 1.5;    // crops this much taller than wide are rotated
static constexpr float  Half      = 0.5f;
static constexpr double MaxPixel  = 255.0;

// ---------------------------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------------------------
static std::string Trim(std::string const& s)
{
    auto const b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

static std::string OneLine(std::string s)
{
    std::ranges::replace_if(s, [](char c) { return c == '\n' || c == '\r' || c == '\t'; }, ' ');
    return Trim(s);
}

// Code point at byte `i` of a UTF-8 string (U+FFFD when malformed).
static char32_t DecodeAt(std::string_view s, size_t i)
{
    struct Lead
    {
        unsigned mask;
        unsigned prefix;
        size_t   length;
        unsigned payload;
    };
    static constexpr std::array Leads = {
        Lead{.mask = 0x80, .prefix = 0x00, .length = 1, .payload = 0x7F},
        Lead{.mask = 0xE0, .prefix = 0xC0, .length = 2, .payload = 0x1F},
        Lead{.mask = 0xF0, .prefix = 0xE0, .length = 3, .payload = 0x0F},
        Lead{.mask = 0xF8, .prefix = 0xF0, .length = 4, .payload = 0x07},
    };
    static constexpr unsigned ContPayload = 0x3F;
    static constexpr unsigned ContBits    = 6;
    static constexpr char32_t Bad         = 0xFFFD;
    auto const                byte        = [&](size_t k) { return static_cast<unsigned char>(s.at(k)); };
    for (Lead const& l : Leads)
    {
        if ((byte(i) & l.mask) != l.prefix) continue;
        if (i + l.length > s.size()) return Bad;
        char32_t cp = byte(i) & l.payload;
        for (size_t k = 1; k < l.length; ++k) cp = (cp << ContBits) | (byte(i + k) & ContPayload);
        return cp;
    }
    return Bad;
}

static char32_t LastCodePoint(std::string_view s)
{
    static constexpr unsigned ContMask = 0xC0;
    static constexpr unsigned Cont     = 0x80;
    size_t                    i        = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s.at(i)) & ContMask) == Cont) --i;
    return DecodeAt(s, i);
}

static bool IsCjk(char32_t c)
{
    static constexpr std::array<std::pair<char32_t, char32_t>, 6> Ranges
        = {{{0x2E80, 0x30FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xF900, 0xFAFF}, {0xFF00, 0xFFEF}, {0x20000, 0x2FFFF}}};
    return std::ranges::any_of(Ranges, [c](auto const& r) { return c >= r.first && c <= r.second; });
}

// Join two fragments: drop a line-end hyphen before a lowercase word, no space between CJK, else one space.
static std::string JoinText(std::string const& a, std::string const& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    bool const hyphen = a.size() >= 2 && a.back() == '-' && std::isalpha(static_cast<unsigned char>(a.at(a.size() - 2))) != 0
                        && std::islower(static_cast<unsigned char>(b.front())) != 0;
    if (hyphen) return a.substr(0, a.size() - 1) + b;
    if (IsCjk(LastCodePoint(a)) && IsCjk(DecodeAt(b, 0))) return a + b;
    return a + " " + b;
}

// ---------------------------------------------------------------------------------------------
// Images and tensors
// ---------------------------------------------------------------------------------------------
// BGR image; EXIF orientation applied, transparency composited over white.
static cv::Mat LoadImage(std::string const& path)
{
    static constexpr double Depth16To8 = 1.0 / 257;    // 16-bit -> 8-bit pixel values

    cv::Mat raw = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (raw.empty()) throw std::runtime_error("cannot read image " + path);
    if (raw.channels() != 4) return cv::imread(path, cv::IMREAD_COLOR);
    if (raw.depth() != CV_8U) raw.convertTo(raw, CV_8U, Depth16To8);
    cv::Mat bgr;
    cv::Mat alpha;
    cv::cvtColor(raw, bgr, cv::COLOR_BGRA2BGR);
    cv::extractChannel(raw, alpha, 3);
    alpha.convertTo(alpha, CV_32F, 1.0 / MaxPixel);
    cv::Mat const white(bgr.size(), CV_8UC3, cv::Scalar::all(MaxPixel));
    cv::Mat       out;
    cv::blendLinear(bgr, white, alpha, 1.0 - alpha, out);
    return out;
}

// float HxWx3 image -> 1x3xHxW blob.
static cv::Mat ToBlob(cv::Mat const& img)
{
    cv::Mat              blob(3, img.rows * img.cols, CV_32F);
    std::vector<cv::Mat> planes;
    planes.reserve(3);
    for (int c = 0; c < 3; ++c) planes.push_back(blob.row(c).reshape(1, img.rows));
    cv::split(img, planes);
    return blob;
}

static Ort::Value Tensor(Ort::MemoryInfo const& mem, cv::Mat& data, std::vector<int64_t> const& shape)
{ return Ort::Value::CreateTensor<float>(mem, data.ptr<float>(), data.total(), shape.data(), shape.size()); }

static Model LoadModel(Ort::Env& env, fs::path const& path)
{
    if (!fs::exists(path)) throw std::runtime_error("model not found: " + path.string() + " (run scripts/download_models.sh)");
    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts.DisableMemPattern();    // input shapes change on every call
    Model m;
    m.session = Ort::Session(env, path.c_str(), opts);
    Ort::AllocatorWithDefaultOptions alloc;
    for (size_t i = 0; i < m.session.GetInputCount(); ++i) m.inputNames.emplace_back(m.session.GetInputNameAllocated(i, alloc).get());
    for (size_t i = 0; i < m.session.GetOutputCount(); ++i) m.outputNames.emplace_back(m.session.GetOutputNameAllocated(i, alloc).get());
    return m;
}

static std::vector<Ort::Value> Run(Model& m, std::vector<Ort::Value> const& inputs)
{
    std::vector<char const*> in;
    std::vector<char const*> out;
    in.reserve(m.inputNames.size());
    out.reserve(m.outputNames.size());
    for (auto const& n : m.inputNames) in.push_back(n.c_str());
    for (auto const& n : m.outputNames) out.push_back(n.c_str());
    return m.session.Run(Ort::RunOptions{nullptr}, in.data(), inputs.data(), inputs.size(), out.data(), out.size());
}

// 2-D float view [rows x cols] of an output tensor whose last two dims are rows and cols.
static cv::Mat View(Ort::Value& v)
{
    auto const shape = v.GetTensorTypeAndShapeInfo().GetShape();
    auto const rows  = static_cast<int>(shape.at(shape.size() - 2));
    auto const cols  = static_cast<int>(shape.back());
    return {rows, cols, CV_32F, v.GetTensorMutableData<float>()};
}

static std::vector<std::string> YamlList(fs::path const& file, std::vector<std::string> const& keys)
{
    YAML::Node node = YAML::LoadFile(file.string());
    for (auto const& k : keys)
    {
        auto const it = std::ranges::find_if(node, [&](auto const& kv) { return kv.first.template as<std::string>() == k; });
        if (it == node.end()) throw std::runtime_error("key '" + k + "' not found in " + file.string());
        node = it->second;
    }
    if (!node.IsSequence()) throw std::runtime_error("no list at the expected key in " + file.string());
    std::vector<std::string> items;
    for (auto const& item : node) items.push_back(item.as<std::string>());
    return items;
}

// ---------------------------------------------------------------------------------------------
// Layout (PaddleX LayoutAnalysisProcess, rectangle mode)
// ---------------------------------------------------------------------------------------------
static float IouPlus1(cv::Rect2f const& a, cv::Rect2f const& b)    // inclusive pixel coordinates, as in PaddleX nms()
{
    float const iw    = std::max(0.f, std::min(a.br().x, b.br().x) - std::max(a.x, b.x) + 1);
    float const ih    = std::max(0.f, std::min(a.br().y, b.br().y) - std::max(a.y, b.y) + 1);
    float const inter = iw * ih;
    return inter / (((a.width + 1) * (a.height + 1)) + ((b.width + 1) * (b.height + 1)) - inter);
}

static float OverlapOfSmaller(cv::Rect2f const& a, cv::Rect2f const& b)
{
    float const small = std::min(a.area(), b.area());
    return small > 0 ? (a & b).area() / small : 0.f;
}

static std::vector<LayoutBox> Nms(std::vector<LayoutBox> boxes)
{
    static constexpr float NmsSameClass  = 0.6f;
    static constexpr float NmsOtherClass = 0.98f;

    std::ranges::stable_sort(boxes, std::greater{}, &LayoutBox::score);
    std::vector<LayoutBox> kept;
    for (auto const& b : boxes)
    {
        bool const suppressed = std::ranges::any_of(
            kept, [&](LayoutBox const& k) { return IouPlus1(k.box, b.box) >= (k.cls == b.cls ? NmsSameClass : NmsOtherClass); });
        if (!suppressed) kept.push_back(b);
    }
    return kept;
}

// Drop "image" boxes covering (almost) the whole page, unless that would drop everything.
static std::vector<LayoutBox> DropPageImages(std::vector<LayoutBox> const& boxes, std::vector<std::string> const& labels, cv::Size page)
{
    static constexpr float PageImageLand = 0.82f;    // drop "image" blocks covering this much of a landscape page
    static constexpr float PageImagePort = 0.93f;    // ... of a portrait page

    if (boxes.size() <= 1) return boxes;
    float const            limit = (page.width > page.height ? PageImageLand : PageImagePort) * static_cast<float>(page.area());
    cv::Rect2f const       pageRect(0, 0, static_cast<float>(page.width), static_cast<float>(page.height));
    std::vector<LayoutBox> kept;
    std::ranges::copy_if(boxes, std::back_inserter(kept), [&](LayoutBox const& b) {
        return labels.at(b.cls) != "image" || (b.box & pageRect).area() <= limit;
    });
    return kept.empty() ? boxes : kept;
}

// Drop boxes lying (>= 90%) inside a chart / formula / title box.
static std::vector<LayoutBox> DropNested(std::vector<LayoutBox> const& boxes, std::vector<std::string> const& labels)
{
    static constexpr float ContainedRatio = 0.9f;

    static constexpr auto Large
        = std::to_array<std::string_view>({"chart", "display_formula", "doc_title", "inline_formula", "paragraph_title"});
    std::vector<LayoutBox> kept;
    for (auto const& b : boxes)
    {
        bool const nested = std::ranges::any_of(boxes, [&](LayoutBox const& o) {
            return &o != &b && std::ranges::contains(Large, labels.at(o.cls)) && b.box.area() > 0
                   && (b.box & o.box).area() / b.box.area() >= ContainedRatio;
        });
        if (!nested) kept.push_back(b);
    }
    return kept;
}

// PaddleX filter_boxes(): drop tiny boxes, inline formulas inside text, and the smaller of two overlapping boxes.
static std::vector<Block> FilterOverlaps(std::vector<LayoutBox> boxes, std::vector<std::string> const& labels)
{
    static constexpr float OverlapRatio  = 0.7f;
    static constexpr float InlineOverlap = 0.5f;
    static constexpr float MinBlockSide  = 6;

    static constexpr auto Visual = std::to_array<std::string_view>({"image", "table", "seal", "chart"});
    std::erase_if(boxes, [&](LayoutBox const& b) { return labels.at(b.cls) == "reference"; });
    std::vector<bool> drop(boxes.size(), false);
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        if (boxes.at(i).box.width < MinBlockSide || boxes.at(i).box.height < MinBlockSide) drop.at(i) = true;
        for (size_t j = i + 1; j < boxes.size() && !drop.at(i); ++j)
        {
            if (drop.at(j)) continue;
            std::string const& li      = labels.at(boxes.at(i).cls);
            std::string const& lj      = labels.at(boxes.at(j).cls);
            float const        overlap = OverlapOfSmaller(boxes.at(i).box, boxes.at(j).box);
            if (li == "inline_formula" || lj == "inline_formula")
            {
                if (overlap > InlineOverlap)
                {
                    drop.at(i) = drop.at(i) || li == "inline_formula";
                    drop.at(j) = drop.at(j) || lj == "inline_formula";
                }
                continue;
            }
            auto const hasli       = std::ranges::contains(Visual, li);
            auto const haslj       = std::ranges::contains(Visual, lj);
            bool const mixedVisual = li != lj && (hasli || haslj);
            bool const tableInside = (li == "table" || lj == "table") && !(hasli && haslj);
            if (overlap <= OverlapRatio || (mixedVisual && !tableInside)) continue;
            drop.at(boxes.at(i).box.area() >= boxes.at(j).box.area() ? j : i) = true;
        }
    }
    std::vector<Block> blocks;
    for (size_t i = 0; i < boxes.size(); ++i)
        if (!drop.at(i))
            blocks.push_back(
                {.label = labels.at(boxes.at(i).cls), .score = boxes.at(i).score, .box = boxes.at(i).box, .lines = {}, .text = {}});
    return blocks;
}

static std::vector<Block> DetectLayout(Models& models, cv::Mat const& img)
{
    static constexpr int   LayoutSize   = 800;
    static constexpr float LayoutThresh = 0.3f;

    cv::Mat x;
    cv::cvtColor(img, x, cv::COLOR_BGR2RGB);
    cv::resize(x, x, {LayoutSize, LayoutSize}, 0, 0, cv::INTER_CUBIC);
    x.convertTo(x, CV_32FC3, 1.0 / MaxPixel);
    cv::Mat                 blob = ToBlob(x);
    cv::Mat                 imShape(1, 2, CV_32F, cv::Scalar(LayoutSize));
    cv::Mat                 scale(cv::Matx12f(static_cast<float>(LayoutSize) / static_cast<float>(img.rows),
                                              static_cast<float>(LayoutSize) / static_cast<float>(img.cols)));
    std::vector<Ort::Value> inputs;
    for (auto const& name : models.layout.inputNames)
    {
        if (name == "image") inputs.push_back(Tensor(models.layout.mem, blob, {1, 3, LayoutSize, LayoutSize}));
        else if (name == "im_shape") inputs.push_back(Tensor(models.layout.mem, imShape, {1, 2}));
        else inputs.push_back(Tensor(models.layout.mem, scale, {1, 2}));
    }
    auto                   out   = Run(models.layout, inputs);
    cv::Mat const          pred  = View(out.front());    // N x 7: class, score, x0, y0, x1, y1, reading order
    static constexpr int   Cls   = 0;                    // columns of the prediction rows
    static constexpr int   Score = 1;
    static constexpr int   X0    = 2;
    static constexpr int   Y0    = 3;
    static constexpr int   X1    = 4;
    static constexpr int   Y1    = 5;
    static constexpr int   Order = 6;
    std::vector<LayoutBox> boxes;
    for (int i = 0; i < pred.rows; ++i)
    {
        auto const cls = static_cast<int>(pred.at<float>(i, Cls));
        if (pred.at<float>(i, Score) <= LayoutThresh || cls < 0 || std::cmp_greater_equal(cls, models.labels.size())) continue;
        auto const r = [&](int c) { return std::nearbyint(pred.at<float>(i, c)); };
        boxes.push_back({
            .cls   = cls,
            .score = pred.at<float>(i, Score),
            .box   = cv::Rect2f(cv::Point2f(r(X0), r(Y0)), cv::Point2f(r(X1), r(Y1))),
            .order = pred.cols > Order ? pred.at<float>(i, Order) : static_cast<float>(i),
        });
    }
    boxes = DropNested(DropPageImages(Nms(boxes), models.labels, img.size()), models.labels);
    std::ranges::stable_sort(boxes, std::less{}, &LayoutBox::order);
    cv::Rect2f const pageRect(0, 0, static_cast<float>(img.cols), static_cast<float>(img.rows));
    for (auto& b : boxes) b.box &= pageRect;
    std::erase_if(boxes, [](LayoutBox const& b) { return b.box.area() <= 0; });
    return FilterOverlaps(boxes, models.labels);
}

// ---------------------------------------------------------------------------------------------
// Text detection (PP-OCRv6 DB head, PaddleX DBPostProcess) and recognition (CTC)
// ---------------------------------------------------------------------------------------------
// cv::minAreaRect corners ordered like PaddleOCR get_mini_boxes(); also returns the short side.
static std::pair<Quad, float> MiniBox(std::vector<cv::Point> const& pts)
{
    cv::RotatedRect const r = cv::minAreaRect(pts);
    Quad                  p{};
    r.points(p.data());
    std::ranges::stable_sort(p, std::less{}, &cv::Point2f::x);
    bool const leftDown  = p.at(1).y > p.at(0).y;
    bool const rightDown = p.at(3).y > p.at(2).y;
    Quad const q
        = {leftDown ? p.at(0) : p.at(1), rightDown ? p.at(2) : p.at(3), rightDown ? p.at(3) : p.at(2), leftDown ? p.at(1) : p.at(0)};
    return {q, std::min(r.size.width, r.size.height)};
}

static std::vector<cv::Point> ToInt(Quad const& q)
{ return {cv::Point(q.at(0)), cv::Point(q.at(1)), cv::Point(q.at(2)), cv::Point(q.at(3))}; }

// Mean probability inside the box (cv::fillPoly + cv::mean, as box_score_fast).
static double BoxScore(cv::Mat const& prob, Quad const& box)
{
    auto const [minX, maxX]   = std::ranges::minmax(box | std::views::transform(&cv::Point2f::x));
    auto const [minY, maxY]   = std::ranges::minmax(box | std::views::transform(&cv::Point2f::y));
    int const              x0 = std::clamp(static_cast<int>(std::floor(minX)), 0, prob.cols - 1);
    int const              x1 = std::clamp(static_cast<int>(std::ceil(maxX)), 0, prob.cols - 1);
    int const              y0 = std::clamp(static_cast<int>(std::floor(minY)), 0, prob.rows - 1);
    int const              y1 = std::clamp(static_cast<int>(std::ceil(maxY)), 0, prob.rows - 1);
    std::vector<cv::Point> pts;
    for (auto const& p : box)
        pts.emplace_back(static_cast<int>(p.x - static_cast<float>(x0)), static_cast<int>(p.y - static_cast<float>(y0)));
    cv::Mat mask = cv::Mat::zeros(y1 - y0 + 1, x1 - x0 + 1, CV_8U);
    cv::fillPoly(mask, std::vector{pts}, 1);
    return cv::mean(prob(cv::Rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1)), mask)(0);
}

// Offset the box by area * ratio / perimeter (pyclipper JT_ROUND in PaddleOCR).
static std::vector<cv::Point> Unclip(Quad const& box)
{
    static constexpr double DetUnclipRatio = 1.5;    // box dilation

    std::vector<cv::Point2f> const poly(box.begin(), box.end());
    double const                   distance = cv::contourArea(poly) * DetUnclipRatio / cv::arcLength(poly, true);
    Clipper2Lib::Path64            path;
    for (auto const& p : box) path.emplace_back(static_cast<int64_t>(p.x), static_cast<int64_t>(p.y));
    Clipper2Lib::Paths64 const grown
        = Clipper2Lib::InflatePaths({path}, distance, Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon);
    std::vector<cv::Point> out;
    if (!grown.empty())
        for (auto const& p : grown.front()) out.emplace_back(static_cast<int>(p.x), static_cast<int>(p.y));
    return out;
}

// PaddleOCR SortQuadBoxes: top to bottom, left to right within ~10 px.
static void SortReadingOrder(std::vector<Quad>& quads)
{
    static constexpr float SortRowTolerance = 10;

    std::ranges::stable_sort(
        quads, [](Quad const& a, Quad const& b) { return std::pair(a.at(0).y, a.at(0).x) < std::pair(b.at(0).y, b.at(0).x); });
    for (size_t i = 0; i + 1 < quads.size(); ++i)
        for (size_t j = i + 1; j-- > 0;)
        {
            if (std::abs(quads.at(j + 1).at(0).y - quads.at(j).at(0).y) >= SortRowTolerance
                || quads.at(j + 1).at(0).x >= quads.at(j).at(0).x)
                break;
            std::swap(quads.at(j), quads.at(j + 1));
        }
}

static std::vector<Quad> DetectLines(Models& models, cv::Mat const& img)
{

    static constexpr std::array<double, 3> ImageNetMean     = {0.485, 0.456, 0.406};
    static constexpr std::array<double, 3> ImageNetStd      = {0.229, 0.224, 0.225};
    static constexpr double                DetThresh        = 0.3;     // DB pixel probability threshold
    static constexpr double                DetBoxThresh     = 0.6;     // mean probability inside a box
    static constexpr int                   DetLimitSide     = 64;      // upscale so that the short side >= this
    static constexpr int                   DetMaxSide       = 4000;    // downscale so that the long side <= this
    static constexpr int                   DetStride        = 32;
    static constexpr std::ptrdiff_t        DetMaxCandidates = 1000;
    static constexpr float                 DetMinSize       = 3;

    cv::Mat src = img;    // pad tiny images to >= 32 x 32 (bottom/right)
    if (img.cols + img.rows < 2 * DetStride)
        cv::copyMakeBorder(img, src, 0, std::max(0, DetStride - img.rows), 0, std::max(0, DetStride - img.cols), cv::BORDER_CONSTANT, 0);
    // DetResizeForTest(limit_type=min): upscale small images, cap the long side, round to a multiple of 32
    double ratio = std::min(src.cols, src.rows) < DetLimitSide ? static_cast<double>(DetLimitSide) / std::min(src.cols, src.rows) : 1.0;
    int    rh    = static_cast<int>(src.rows * ratio);
    int    rw    = static_cast<int>(src.cols * ratio);
    if (std::max(rh, rw) > DetMaxSide)
    {
        ratio = static_cast<double>(DetMaxSide) / std::max(rh, rw);
        rh    = static_cast<int>(rh * ratio);
        rw    = static_cast<int>(rw * ratio);
    }
    auto const round32
        = [](int v) { return std::max(static_cast<int>(std::nearbyint(v / static_cast<double>(DetStride))) * DetStride, DetStride); };
    rh = round32(rh);
    rw = round32(rw);
    cv::Mat x;
    cv::resize(src, x, {rw, rh});
    x.convertTo(x, CV_32FC3, 1.0 / MaxPixel);    // BGR with ImageNet mean/std in BGR order, as PaddleOCR
    cv::subtract(x, cv::Scalar(ImageNetMean.at(0), ImageNetMean.at(1), ImageNetMean.at(2)), x);
    cv::divide(x, cv::Scalar(ImageNetStd.at(0), ImageNetStd.at(1), ImageNetStd.at(2)), x);
    cv::Mat                 blob = ToBlob(x);
    std::vector<Ort::Value> inputs;
    inputs.push_back(Tensor(models.det.mem, blob, {1, 3, rh, rw}));
    auto          out  = Run(models.det, inputs);
    cv::Mat const prob = View(out.front());

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(prob > DetThresh, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    float const       sx = static_cast<float>(src.cols) / static_cast<float>(prob.cols);
    float const       sy = static_cast<float>(src.rows) / static_cast<float>(prob.rows);
    std::vector<Quad> quads;
    for (auto const& contour : contours | std::views::take(DetMaxCandidates))
    {
        auto const [box, side] = MiniBox(contour);
        if (side < DetMinSize || BoxScore(prob, box) < DetBoxThresh) continue;
        auto const grown = Unclip(box);
        if (grown.empty()) continue;
        auto [q, grownSide] = MiniBox(grown);
        if (grownSide < DetMinSize + 2) continue;
        for (auto& p : q)
        {
            p.x = std::clamp(std::nearbyint(p.x * sx), 0.f, static_cast<float>(img.cols));
            p.y = std::clamp(std::nearbyint(p.y * sy), 0.f, static_cast<float>(img.rows));
        }
        quads.push_back(q);
    }
    SortReadingOrder(quads);
    return quads;
}

// Min-area rectangle of the quad, perspective-warped upright; tall crops are rotated 90 degrees.
static cv::Mat CropLine(cv::Mat const& img, Quad const& quad)
{
    auto const [b, side] = MiniBox(ToInt(quad));
    auto const w         = static_cast<int>(std::max(cv::norm(b.at(0) - b.at(1)), cv::norm(b.at(2) - b.at(3))));
    auto const h         = static_cast<int>(std::max(cv::norm(b.at(0) - b.at(3)), cv::norm(b.at(1) - b.at(2))));
    if (w < 1 || h < 1) return {};
    std::array<cv::Point2f, 4> const dst = {
        cv::Point2f(0, 0),
        cv::Point2f(static_cast<float>(w), 0),
        cv::Point2f(static_cast<float>(w), static_cast<float>(h)),
        cv::Point2f(0, static_cast<float>(h)),
    };
    cv::Mat crop;
    cv::warpPerspective(img, crop, cv::getPerspectiveTransform(b.data(), dst.data()), {w, h}, cv::INTER_CUBIC, cv::BORDER_REPLICATE);
    if (h >= TallRatio * w) cv::rotate(crop, crop, cv::ROTATE_90_COUNTERCLOCKWISE);
    return crop;
}

// 48 px high crop -> CTC greedy decode; returns text and mean character confidence.
static std::pair<std::string, float> Recognize(Models& models, cv::Mat const& line)
{
    static constexpr int RecHeight   = 48;
    static constexpr int RecMinWidth = 320;
    static constexpr int RecMaxWidth = 3200;

    if (line.empty()) return {};
    double const ratio = static_cast<double>(line.cols) / line.rows;
    int          width = static_cast<int>(RecHeight * std::max(static_cast<double>(RecMinWidth) / RecHeight, ratio));
    int          rw    = std::min(width, static_cast<int>(std::ceil(RecHeight * ratio)));
    if (width > RecMaxWidth) width = rw = RecMaxWidth;
    cv::Mat resized;
    cv::resize(line, resized, {std::max(rw, 1), RecHeight});
    cv::Mat                 x(RecHeight, width, CV_32FC3, cv::Scalar::all(0));
    static constexpr double Scale = 2.0 / MaxPixel;    // (v / 255 - 0.5) / 0.5
    resized.convertTo(x(cv::Rect(0, 0, resized.cols, RecHeight)), CV_32FC3, Scale, -1.0);
    cv::Mat                 blob = ToBlob(x);
    std::vector<Ort::Value> inputs;
    inputs.push_back(Tensor(models.rec.mem, blob, {1, 3, RecHeight, width}));
    auto          out   = Run(models.rec, inputs);
    cv::Mat const probs = View(out.front());    // T x classes (softmax)

    std::string text;
    double      conf = 0;
    int         n    = 0;
    int         prev = 0;
    for (int t = 0; t < probs.rows; ++t)
    {
        double    p = 0;
        cv::Point best;
        cv::minMaxLoc(probs.row(t), nullptr, &p, nullptr, &best);
        if (best.x != 0 && best.x != prev && std::cmp_less(best.x, models.charset.size()))
        {
            text += models.charset.at(static_cast<size_t>(best.x));
            conf += p;
            ++n;
        }
        prev = best.x;
    }
    return {text, n > 0 ? static_cast<float>(conf / n) : 0.f};
}

// ---------------------------------------------------------------------------------------------
// Page assembly: lines -> blocks, paragraphs for lines outside any block, reading order
// ---------------------------------------------------------------------------------------------
static float Overlap1D(float a0, float a1, float b0, float b1)
{ return std::min(a1, b1) - std::max(a0, b0); }

// Rows (>= 50% vertical overlap) top to bottom, each left to right.
static std::vector<std::vector<int>> Rows(std::vector<Line> const& lines, std::vector<int> ids)
{
    std::ranges::sort(ids, std::less{}, [&](int i) { return lines.at(i).box.y + (lines.at(i).box.height * Half); });
    std::vector<std::vector<int>> rows;
    float                         top    = 0;
    float                         bottom = 0;
    for (int id : ids)
    {
        cv::Rect2f const& b = lines.at(id).box;
        if (!rows.empty() && Overlap1D(top, bottom, b.y, b.br().y) >= Half * std::min(bottom - top, b.height))
        {
            rows.back().push_back(id);
            top    = std::min(top, b.y);
            bottom = std::max(bottom, b.br().y);
        }
        else
        {
            rows.push_back({id});
            top    = b.y;
            bottom = b.br().y;
        }
    }
    for (auto& r : rows) std::ranges::sort(r, std::less{}, [&](int i) { return lines.at(i).box.x; });
    return rows;
}

// Orders the block's lines and joins their text (tables keep rows, vertical text reads right to left).
static void BlockText(std::vector<Line> const& lines, Block& b)
{
    b.text.clear();
    if (b.label == "vertical_text")
    {
        std::ranges::sort(b.lines, std::greater{}, [&](int i) { return lines.at(i).box.x; });
        for (int id : b.lines) b.text = JoinText(b.text, lines.at(id).text);
        return;
    }
    bool const keepRows = b.label == "table" || b.label == "algorithm";
    for (auto const& row : Rows(lines, std::exchange(b.lines, {})))
    {
        std::string r;
        for (int id : row)
        {
            b.lines.push_back(id);
            if (keepRows && !r.empty())
            {
                r += b.label == "table" ? " | " : " ";
                r += lines.at(id).text;
            }
            else
            {
                r = JoinText(r, lines.at(id).text);
            }
        }
        if (!keepRows) { b.text = JoinText(b.text, r); }
        else
        {
            if (!b.text.empty()) b.text += '\n';
            b.text += r;
        }
    }
}

static bool SameParagraph(cv::Rect2f const& a, cv::Rect2f const& b)
{
    static constexpr float SameRowGap    = 0.6f;    // gap allowed between pieces of one row, in line heights
    static constexpr float StackedHeight = 1.3f;    // max height ratio of stacked lines
    static constexpr float StackedGap    = 0.8f;    // max vertical gap, in line heights
    static constexpr float StackedShared = 0.3f;    // min horizontal overlap of stacked lines
    float const            h             = std::min(a.height, b.height);
    float const            hMax          = std::max(a.height, b.height);
    float const            vov           = Overlap1D(a.y, a.br().y, b.y, b.br().y);
    float const            hov           = Overlap1D(a.x, a.br().x, b.x, b.br().x);
    if (vov >= Half * h && -hov <= SameRowGap * hMax) return true;
    return hMax <= StackedHeight * h && -vov <= StackedGap * hMax && hov > StackedShared * std::min(a.width, b.width);
}

// Recursive XY-cut, columns first.
static void XyCut(std::vector<int> ids, std::vector<cv::Rect2f> const& boxes, std::vector<int>& out)
{
    if (ids.size() <= 1)
    {
        out.insert(out.end(), ids.begin(), ids.end());
        return;
    }
    for (bool const columns : {true, false})
    {
        auto lo = [&](auto i) { return columns ? boxes.at(i).x : boxes.at(i).y; };
        auto hi = [&](auto i) { return columns ? boxes.at(i).br().x : boxes.at(i).br().y; };
        std::ranges::sort(ids, std::less{}, lo);
        float reach = hi(ids.front());
        for (size_t k = 1; k < ids.size(); ++k)
        {
            if (lo(ids.at(k)) > reach)
            {
                auto const mid = ids.begin() + static_cast<std::ptrdiff_t>(k);
                XyCut(std::vector<int>(ids.begin(), mid), boxes, out);
                XyCut(std::vector<int>(mid, ids.end()), boxes, out);
                return;
            }
            reach = std::max(reach, hi(ids.at(k)));
        }
    }
    std::ranges::sort(ids, [&](int a, int b) { return std::pair(boxes.at(a).y, boxes.at(a).x) < std::pair(boxes.at(b).y, boxes.at(b).x); });
    out.insert(out.end(), ids.begin(), ids.end());
}

// Lines outside every layout block -> paragraph groups (union-find), ordered by XY-cut.
static std::vector<Block> GroupOrphans(std::vector<Line> const& lines)
{
    std::vector<int> orphans;
    for (size_t i = 0; i < lines.size(); ++i)
        if (lines.at(i).block < 0) orphans.push_back(static_cast<int>(i));
    std::vector<size_t> parent(orphans.size());
    std::ranges::iota(parent, size_t{0});
    std::function<size_t(size_t)> find = [&](size_t x) { return parent.at(x) == x ? x : parent.at(x) = find(parent.at(x)); };
    for (size_t a = 0; a < orphans.size(); ++a)
        for (size_t b = a + 1; b < orphans.size(); ++b)
            if (SameParagraph(lines.at(orphans.at(a)).box, lines.at(orphans.at(b)).box)) parent.at(find(a)) = find(b);
    std::vector<Block> groups;
    std::vector<int>   groupOf(orphans.size(), -1);
    for (size_t a = 0; a < orphans.size(); ++a)
    {
        size_t const root = find(a);
        if (groupOf.at(root) < 0)
        {
            groupOf.at(root) = static_cast<int>(groups.size());
            groups.push_back({.label = "text", .score = 0, .box = lines.at(orphans.at(a)).box, .lines = {}, .text = {}});
        }
        Block& g = groups.at(static_cast<size_t>(groupOf.at(root)));
        g.box |= lines.at(orphans.at(a)).box;
        g.lines.push_back(orphans.at(a));
    }
    std::vector<cv::Rect2f> boxes;
    boxes.reserve(groups.size());
    for (auto const& g : groups) boxes.push_back(g.box);
    std::vector<int> all(groups.size());
    std::iota(all.begin(), all.end(), 0);
    std::vector<int> order;
    XyCut(all, boxes, order);
    std::vector<Block> sorted;
    sorted.reserve(order.size());
    for (int i : order) sorted.push_back(std::move(groups.at(static_cast<size_t>(i))));
    return sorted;
}

static void Assemble(Page& page)
{
    static constexpr float TieEps      = 1e-3f;
    static constexpr float LineInBlock = 0.5f;    // a line belongs to the block covering this much of it

    // each line goes to the block covering most of it (>= 50%); ties go to the smaller block
    for (size_t i = 0; i < page.lines.size(); ++i)
    {
        Line&       line     = page.lines.at(i);
        float const area     = std::max(line.box.area(), 1.f);
        float       best     = 0;
        float       bestArea = 0;
        for (size_t b = 0; b < page.blocks.size(); ++b)
        {
            float const r     = (line.box & page.blocks.at(b).box).area() / area;
            float const bArea = page.blocks.at(b).box.area();
            if (r > best + TieEps || (r > 0 && std::abs(r - best) <= TieEps && bArea < bestArea))
            {
                best       = r;
                bestArea   = bArea;
                line.block = static_cast<int>(b);
            }
        }
        if (best < LineInBlock) line.block = -1;
        if (line.block >= 0) page.blocks.at(static_cast<size_t>(line.block)).lines.push_back(static_cast<int>(i));
    }
    // leftover lines: insert each paragraph before the first block that starts below it in the same column
    for (auto& g : GroupOrphans(page.lines))
    {
        auto const pos = std::ranges::find_if(
            page.blocks, [&](Block const& b) { return b.box.y > g.box.y && Overlap1D(b.box.x, b.box.br().x, g.box.x, g.box.br().x) > 0; });
        page.blocks.insert(pos, std::move(g));
    }
    // text per block, then renumber lines in reading order
    std::vector<Line> ordered;
    for (size_t b = 0; b < page.blocks.size(); ++b)
    {
        BlockText(page.lines, page.blocks.at(b));
        for (int& id : page.blocks.at(b).lines)
        {
            ordered.push_back(page.lines.at(static_cast<size_t>(id)));
            ordered.back().block = static_cast<int>(b);
            id                   = static_cast<int>(ordered.size() - 1);
        }
    }
    page.lines = std::move(ordered);
}

// ---------------------------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------------------------
static std::string ToMarkdown(Page const& page)
{
    std::string md;
    for (auto const& b : page.blocks)
    {
        std::string const t = Trim(b.text);
        if (t.empty()) continue;
        if (!md.empty()) md += "\n\n";
        if (b.label == "doc_title") md += "# " + OneLine(t);
        else if (b.label == "paragraph_title") md += "## " + OneLine(t);
        else md += t;
    }
    return md + "\n";
}

static Json ToJson(Page const& page, std::string const& path, cv::Size size)
{
    auto const ints = [](cv::Rect2f const& r) {
        return Json::array({std::lround(r.x), std::lround(r.y), std::lround(r.br().x), std::lround(r.br().y)});
    };
    static constexpr double Digits = 1e4;
    auto const              round4 = [](float v) { return std::round(static_cast<double>(v) * Digits) / Digits; };
    Json                    blocks = Json::array();
    Json                    lines  = Json::array();
    for (size_t i = 0; i < page.blocks.size(); ++i)
    {
        Block const& b = page.blocks.at(i);
        blocks.push_back(
            {{"id", i}, {"label", b.label}, {"score", round4(b.score)}, {"bbox", ints(b.box)}, {"text", b.text}, {"lines", b.lines}});
    }
    for (size_t i = 0; i < page.lines.size(); ++i)
    {
        Line const& l    = page.lines.at(i);
        Json        quad = Json::array();
        for (auto const& p : l.quad) quad.push_back({std::lround(p.x), std::lround(p.y)});
        lines.push_back(
            {{"id", i}, {"block", l.block}, {"bbox", ints(l.box)}, {"quad", quad}, {"score", round4(l.score)}, {"text", l.text}});
    }
    return {{"image", {{"path", path}, {"width", size.width}, {"height", size.height}}}, {"blocks", blocks}, {"lines", lines}};
}

// Writes `text` into the quad, scaled to its height and stretched (<= 2x) or shrunk to its width.
static void DrawText(cv::Mat& img, Quad const& q, std::string const& text, cv::freetype::FreeType2& font, cv::Scalar const& color)
{
    static constexpr float  FontScale  = 0.8f;    // font size relative to the box height
    static constexpr double MaxStretch = 2.0;
    static constexpr float  PadW       = 0.01f;
    static constexpr float  PadH       = 0.03f;
    Quad                    dst        = q;    // text corners tl, tr, br, bl -> image points
    auto                    w          = static_cast<float>(std::max(cv::norm(q.at(0) - q.at(1)), cv::norm(q.at(3) - q.at(2))));
    auto                    h          = static_cast<float>(std::max(cv::norm(q.at(0) - q.at(3)), cv::norm(q.at(1) - q.at(2))));
    if (h >= TallRatio * w)
    {    // vertical line: the recognizer read it rotated 90 degrees counter-clockwise
        dst = {q.at(1), q.at(2), q.at(3), q.at(0)};
        std::swap(w, h);
    }
    int const   fontPx = static_cast<int>(FontScale * h);
    float const pad    = (PadW * w) + (PadH * h);
    float const avail  = w - (2 * pad);
    if (text.empty() || fontPx < 3 || avail < 2) return;
    int            baseline = 0;
    cv::Size const size     = font.getTextSize(text, fontPx, -1, &baseline);
    cv::Mat        canvas(size.height + baseline + 2, size.width + 2, CV_8UC1, cv::Scalar(0));
    font.putText(canvas, text, {1, size.height}, fontPx, cv::Scalar(MaxPixel), -1, cv::LINE_AA, true);    // org = baseline
    // canvas -> text rectangle (scale, offset) -> quad (homography) -> region of interest
    double const                     fit = avail / static_cast<double>(canvas.cols);
    double const                     sx  = std::min(fit, MaxStretch);
    double const                     sy  = std::min(fit, 1.0);
    cv::Matx33d const                toRect(sx, 0, pad, 0, sy, (h - (sy * canvas.rows)) * Half, 0, 0, 1);
    std::array<cv::Point2f, 4> const rect = {cv::Point2f(0, 0), cv::Point2f(w, 0), cv::Point2f(w, h), cv::Point2f(0, h)};
    cv::Matx33d const                toQuad(cv::getPerspectiveTransform(rect.data(), dst.data()));
    cv::Rect const roi = cv::boundingRect(std::vector<cv::Point2f>(q.begin(), q.end())) & cv::Rect(0, 0, img.cols, img.rows);
    if (roi.empty()) return;
    cv::Matx33d const toRoi(1, 0, -roi.x, 0, 1, -roi.y, 0, 0, 1);
    cv::Mat           alpha;
    cv::warpPerspective(canvas, alpha, cv::Mat(toRoi * toQuad * toRect), roi.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, 0);
    alpha.convertTo(alpha, CV_32F, 1.0 / MaxPixel);
    cv::Mat const ink(roi.size(), CV_8UC3, color);
    cv::Mat       region = img(roi);
    cv::blendLinear(ink, region, alpha, 1.0 - alpha, region);
}

// original | gray divider | faded line boxes with the recognised text in red.
static cv::Mat DrawOverlay(cv::Mat const& img, Page const& page, cv::Ptr<cv::freetype::FreeType2> const& font)
{
    static constexpr double Fade        = 0.8;     // how far the original fades to white inside line boxes
    static constexpr int    ThickEvery  = 1000;    // outline thickness: 1 px per this many image pixels
    static constexpr int    DividerDiv  = 150;
    static constexpr double DividerGray = 0.6;
    static cv::Scalar const Red(0, 0, 230);

    cv::Mat                             annotated = img.clone();
    std::vector<std::vector<cv::Point>> polys;
    polys.reserve(page.lines.size());
    for (auto const& l : page.lines) polys.push_back(ToInt(l.quad));
    cv::Mat faded;
    cv::addWeighted(img, 1.0 - Fade, cv::Mat(img.size(), img.type(), cv::Scalar::all(MaxPixel)), Fade, 0, faded);
    cv::Mat mask = cv::Mat::zeros(img.size(), CV_8U);
    cv::fillPoly(mask, polys, 1);
    faded.copyTo(annotated, mask);
    if (!font.empty())
        for (auto const& l : page.lines) DrawText(annotated, l.quad, l.text, *font, Red);
    int const thickness = std::max(1, static_cast<int>(std::lround(std::min(img.cols, img.rows) / static_cast<double>(ThickEvery))));
    cv::polylines(annotated, polys, true, Red, thickness, cv::LINE_AA);
    cv::Mat const divider(img.rows, std::max(4, img.cols / DividerDiv), CV_8UC3, cv::Scalar::all(MaxPixel * DividerGray));
    cv::Mat       out;
    cv::hconcat(std::vector{img, divider, annotated}, out);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------------------------
static Models LoadModels(Ort::Env& env, fs::path const& dir)
{
#define HFBASEURL(x) (std::string_view("https://huggingface.co/PaddlePaddle/" x))    // NOLINT

    static constexpr auto UrlLayout = HFBASEURL("PP-DocLayoutV3_onnx/resolve/46bbdf188bb0a772c08aed74882ce7e51a8f1ea6/inference.onnx");
    static constexpr auto UrlDet    = HFBASEURL("PP-OCRv6_medium_det_onnx/resolve/61323801669c338b7891481ec7bac61ce31b576a/inference.onnx");
    static constexpr auto UrlRec    = HFBASEURL("PP-OCRv6_medium_rec_onnx/resolve/50c7eacafc52fa7bcf4194e8cd08e46f8558504b/inference.onnx");
    static constexpr auto UrlLayoutCfg = HFBASEURL("PP-DocLayoutV3_onnx/resolve/46bbdf188bb0a772c08aed74882ce7e51a8f1ea6/inference.yml");
    static constexpr auto UrlRecCfg = HFBASEURL("PP-OCRv6_medium_rec_onnx/resolve/50c7eacafc52fa7bcf4194e8cd08e46f8558504b/inference.yml");
    static constexpr std::string_view NotoSansSC
        = "https://raw.githubusercontent.com/google/fonts/7af0d90b4d532ffa4b041937b560c6df4e7b1e95/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf";

    std::filesystem::create_directories(dir / "fonts");
    Models m{
        .layout  = LoadModel(env, DownloadFile(UrlLayout, dir / "PP-DocLayoutV3/inference.onnx")),
        .det     = LoadModel(env, DownloadFile(UrlDet, dir / "PP-OCRv6_medium_det/inference.onnx")),
        .rec     = LoadModel(env, DownloadFile(UrlRec, dir / "PP-OCRv6_medium_rec/inference.onnx")),
        .labels  = YamlList(DownloadFile(UrlLayoutCfg, dir / "PP-DocLayoutV3/inference.yml"), {"label_list"}),
        .charset = {},
        .font    = {},
    };
    auto dict = YamlList(DownloadFile(UrlRecCfg, dir / "PP-OCRv6_medium_rec/inference.yml"), {"PostProcess", "character_dict"});
    m.charset.reserve(dict.size() + 2);
    m.charset.emplace_back();    // CTC blank
    std::ranges::move(dict, std::back_inserter(m.charset));
    m.charset.emplace_back(" ");    // use_space_char
    auto const classes = m.rec.session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape().back();
    if (classes > 0 && std::cmp_not_equal(classes, m.charset.size()))
        throw std::runtime_error("recognizer dictionary does not match the model");
    m.font = cv::freetype::createFreeType2();
    m.font->loadFontData(DownloadFile(NotoSansSC, dir / "fonts/NotoSansSC.ttf").string(), 0);

    return m;
}

using namespace std::string_view_literals;

struct ScannerImpl : Scanner
{
    ScannerImpl(CommonOptions const& optsIn) :
        Scanner(optsIn), ortEnv(ORT_LOGGING_LEVEL_ERROR, "papertrail.scanner"), m(LoadModels(ortEnv, optsIn.workDir / "models"))
    {
        maxThreads = 1;
        Start();
        cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);    // we report unreadable files ourselves
    }

    ~ScannerImpl() override = default;
    CLASS_DELETE_COPY_AND_MOVE(ScannerImpl);

    void ScanFile(std::filesystem::path const& fpath, std::filesystem::path const& outdir) override;

    Ort::Env ortEnv;
    Models   m;
};

void ScannerImpl::ScanFile(std::filesystem::path const& fpath, std::filesystem::path const& outdir)
{
    static constexpr float RecMinScore = 0.3f;    // drop lines recognised with lower confidence
    static constexpr int   JpegQuality = 90;

    auto outf    = outdir / "index.txt";
    auto outftmp = outf;
    outftmp.replace_extension(".tmp");
    fmt::print("Scanning file: {} to {} ...", fpath, outf);
    std::fflush(nullptr);
    auto          startAt = std::chrono::steady_clock::now();
    auto const    t0      = std::chrono::steady_clock::now();
    cv::Mat const img     = LoadImage(fpath.string());
    Page          page{.blocks = DetectLayout(m, img), .lines = {}};
    for (auto const& q : DetectLines(m, img))
    {
        auto [text, score] = Recognize(m, CropLine(img, q));
        text               = Trim(text);
        if (text.empty() || score < RecMinScore) continue;
        page.lines.push_back(
            {.quad = q, .box = cv::boundingRect(std::vector<cv::Point2f>(q.begin(), q.end())), .text = text, .score = score, .block = -1});
    }
    Assemble(page);

    std::string const name = fpath.stem().string();
    std::ofstream(outdir / (name + ".json")) << ToJson(page, fpath.string(), img.size()).dump(1, ' ', false, Json::error_handler_t::replace)
                                             << "\n";
    std::ofstream(outftmp) << ToMarkdown(page);
    if (!cv::imwrite((outdir / (name + ".overlay.jpg")).string(), DrawOverlay(img, page, m.font), {cv::IMWRITE_JPEG_QUALITY, JpegQuality}))
        throw std::runtime_error("cannot write overlay for " + name);
    std::chrono::duration<double> const dt = std::chrono::steady_clock::now() - t0;
    std::filesystem::rename(outftmp, outf);
    auto endAt = std::chrono::steady_clock::now();
    fmt::print(" done in {}\n", std::chrono::duration_cast<std::chrono::milliseconds>(endAt - startAt));
}

std::unique_ptr<Scanner> Scanner::Create(CommonOptions const& opts)
{ return std::make_unique<ScannerImpl>(opts); }
