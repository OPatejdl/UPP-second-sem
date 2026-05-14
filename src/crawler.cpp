#include "crawler.h"
#include "mpi_helpers.h"
#include "utils.h"
#include "server.h"

#include <mpi.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <deque>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iostream>

// ============================================================
//  URL utilities
// ============================================================

// Return "scheme://domain" prefix of a full URL, e.g. "http://example.com"
static std::string schemeAndDomain(const std::string& url) {
    size_t pos = url.find("://");
    if (pos == std::string::npos) return "";
    size_t pathStart = url.find('/', pos + 3);
    return (pathStart == std::string::npos) ? url : url.substr(0, pathStart);
}

// Return the path component of a URL (strips scheme+domain, fragment, query).
static std::string urlPath(const std::string& url) {
    size_t pos = url.find("://");
    if (pos == std::string::npos) return "/";
    size_t pathStart = url.find('/', pos + 3);
    if (pathStart == std::string::npos) return "/";
    std::string p = url.substr(pathStart);
    // strip fragment
    size_t frag = p.find('#');
    if (frag != std::string::npos) p = p.substr(0, frag);
    // strip query
    size_t qry = p.find('?');
    if (qry != std::string::npos) p = p.substr(0, qry);
    return p;
}

// Resolve ".." and "." segments in a path.
static std::string normalizePath(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (seg != "." && !seg.empty()) {
            parts.push_back(seg);
        }
    }
    if (parts.empty()) return "/";
    std::string result;
    for (const auto& p : parts) result += "/" + p;
    return result;
}

// Return true when `url` is within the crawl scope defined by `baseURL`.
static bool inScope(const std::string& url, const std::string& baseURL) {
    if (url.size() < baseURL.size()) return false;
    if (url.substr(0, baseURL.size()) != baseURL) return false;
    if (url.size() == baseURL.size()) return true;
    // baseURL already ends with '/' → any prefix match is valid
    if (baseURL.back() == '/') return true;
    // otherwise the next character must be '/' (directory boundary)
    return url[baseURL.size()] == '/';
}

// Resolve `href` relative to `currentURL`, filter by `baseURL` scope.
// Returns the normalised full URL, or "" if out of scope / unresolvable.
static std::string resolveURL(const std::string& href,
                               const std::string& currentURL,
                               const std::string& baseURL) {
    if (href.empty() || href[0] == '#') return "";
    if (href.size() >= 11 && href.substr(0, 11) == "javascript:") return "";
    if (href.size() >= 7  && href.substr(0, 7)  == "mailto:") return "";

    std::string full;
    if (href.size() >= 7 && href.substr(0, 7) == "http://") {
        full = href;
    } else if (href.size() >= 8 && href.substr(0, 8) == "https://") {
        full = href;
    } else if (!href.empty() && href[0] == '/') {
        // absolute path on same domain
        full = schemeAndDomain(baseURL) + href;
    } else {
        // relative path: resolve against current page's directory
        std::string curP = urlPath(currentURL);
        size_t slash = curP.rfind('/');
        std::string dir = curP.substr(0, slash + 1);
        full = schemeAndDomain(baseURL) + dir + href;
    }

    // Normalise path
    std::string sd   = schemeAndDomain(full);
    std::string norm = normalizePath(urlPath(full));
    full = sd + norm;

    return inScope(full, baseURL) ? full : "";
}

// Convert a URL to a filesystem-safe name: strip scheme, replace '.' and '/' with '_'.
static std::string urlToSafeName(const std::string& url) {
    std::string s = url;
    size_t pos = s.find("://");
    if (pos != std::string::npos) s = s.substr(pos + 3);
    std::string result;
    for (char c : s) {
        result += (c == '.' || c == '/') ? '_' : c;
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result;
}

// Current time formatted with the given strftime pattern.
static std::string currentTimestamp(const char* fmt) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), fmt, &tm);
    return buf;
}

static std::string trimCR(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

// ============================================================
//  HTML parser
// ============================================================

// Count non-overlapping occurrences of `pattern` in `text`.
static int countTag(const std::string& text, const std::string& pattern) {
    int count = 0;
    size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        ++count;
        pos += pattern.size();
    }
    return count;
}

// Strip all <...> tags from a string (for extracting plain heading text).
static std::string stripTags(const std::string& s) {
    std::string result;
    bool inTag = false;
    for (char c : s) {
        if      (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag)   result += c;
    }
    return result;
}

static std::string trim(const std::string& s) {
    const auto ws = " \t\r\n";
    size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

// Extract href values from <a ...> tags.
static std::vector<std::string> extractHrefs(const std::string& html) {
    std::vector<std::string> hrefs;
    size_t pos = 0;
    while ((pos = html.find("<a ", pos)) != std::string::npos) {
        size_t tagEnd = html.find('>', pos);
        if (tagEnd == std::string::npos) break;

        size_t hrefPos = html.find("href=", pos);
        if (hrefPos == std::string::npos || hrefPos > tagEnd) {
            pos++;
            continue;
        }
        hrefPos += 5; // skip "href="
        if (hrefPos >= html.size()) break;

        char q = html[hrefPos];
        if (q != '"' && q != '\'') { pos++; continue; }

        size_t valStart = hrefPos + 1;
        size_t valEnd   = html.find(q, valStart);
        if (valEnd == std::string::npos || valEnd > tagEnd) { pos++; continue; }

        hrefs.push_back(html.substr(valStart, valEnd - valStart));
        pos = tagEnd + 1;
    }
    return hrefs;
}

// Extract headings h1-h6 in document order, returned as (level, text) pairs.
static std::vector<std::pair<int, std::string>> extractHeadings(const std::string& html) {
    struct Entry { size_t pos; int level; std::string text; };
    std::vector<Entry> entries;

    for (int lvl = 1; lvl <= 6; lvl++) {
        std::string open  = "<h" + std::to_string(lvl);
        std::string close = "</h" + std::to_string(lvl) + ">";
        size_t pos = 0;
        while (true) {
            pos = html.find(open, pos);
            if (pos == std::string::npos) break;
            size_t gtPos = html.find('>', pos);
            if (gtPos == std::string::npos) break;
            size_t closePos = html.find(close, gtPos);
            if (closePos == std::string::npos) break;
            std::string raw = html.substr(gtPos + 1, closePos - gtPos - 1);
            entries.push_back({pos, lvl, trim(stripTags(raw))});
            pos = closePos + close.size();
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.pos < b.pos; });

    std::vector<std::pair<int, std::string>> result;
    for (const auto& e : entries) result.push_back({e.level, e.text});
    return result;
}

// ============================================================
//  PageAnalysis  (Worker B result)
// ============================================================

struct PageAnalysis {
    std::string uri;       // URL path, e.g. "/portal/about"
    int imageCount = 0;
    int linkCount  = 0;
    int formCount  = 0;
    std::vector<std::pair<int, std::string>> headings; // (level, text)
    std::vector<std::string> foundURLs;                // in-scope full URLs
};

static std::string serializePageAnalysis(const PageAnalysis& pa) {
    std::ostringstream oss;
    oss << "URI:" << pa.uri << "\n"
        << "IMAGES:" << pa.imageCount << "\n"
        << "LINKS:"  << pa.linkCount  << "\n"
        << "FORMS:"  << pa.formCount  << "\n";
    for (const auto& [lvl, txt] : pa.headings)
        oss << "HEADING:" << lvl << ":" << txt << "\n";
    for (const auto& u : pa.foundURLs)
        oss << "FOUNDURL:" << u << "\n";
    return oss.str();
}

static PageAnalysis deserializePageAnalysis(const std::string& data) {
    PageAnalysis pa;
    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        line = trimCR(line);
        if (line.substr(0, 4)  == "URI:")     pa.uri        = line.substr(4);
        else if (line.substr(0, 7)  == "IMAGES:") pa.imageCount = std::stoi(line.substr(7));
        else if (line.substr(0, 6)  == "LINKS:")  pa.linkCount  = std::stoi(line.substr(6));
        else if (line.substr(0, 6)  == "FORMS:")  pa.formCount  = std::stoi(line.substr(6));
        else if (line.substr(0, 8)  == "HEADING:") {
            std::string rest = line.substr(8);
            size_t colon = rest.find(':');
            if (colon != std::string::npos)
                pa.headings.push_back({std::stoi(rest.substr(0, colon)), rest.substr(colon + 1)});
        }
        else if (line.substr(0, 9) == "FOUNDURL:") pa.foundURLs.push_back(line.substr(9));
    }
    return pa;
}

// ============================================================
//  CrawlResult  (Worker A → Master)
// ============================================================

struct CrawlResult {
    std::string baseURL;
    std::string startTime;
    std::string endTime;
    std::string mapText;
    std::string contentText;
    bool        ok    = true;
    std::string error;
};

static std::string serializeCrawlResults(const std::vector<CrawlResult>& results) {
    std::ostringstream oss;
    for (const auto& r : results) {
        oss << "BASEURL:"  << r.baseURL   << "\n"
            << "START:"    << r.startTime << "\n"
            << "END:"      << r.endTime   << "\n"
            << "OK:"       << (r.ok ? "1" : "0") << "\n";
        if (!r.ok) oss << "ERROR:" << r.error << "\n";
        oss << "MAP_START\n" << r.mapText     << "MAP_END\n"
            << "CONTENT_START\n" << r.contentText << "CONTENT_END\n";
    }
    return oss.str();
}

static std::vector<CrawlResult> deserializeCrawlResults(const std::string& data) {
    std::vector<CrawlResult> results;
    CrawlResult cur;
    bool hasCur = false, inMap = false, inContent = false;

    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        line = trimCR(line);
        if (line.substr(0, 8) == "BASEURL:") {
            if (hasCur) results.push_back(cur);
            cur = CrawlResult{};
            cur.baseURL = line.substr(8);
            hasCur = true; inMap = false; inContent = false;
        } else if (line.substr(0, 6)  == "START:")   { cur.startTime   = line.substr(6); }
        else if (line.substr(0, 4)    == "END:")      { cur.endTime     = line.substr(4); }
        else if (line.substr(0, 3)    == "OK:")       { cur.ok          = (line[3] == '1'); }
        else if (line.substr(0, 6)    == "ERROR:")    { cur.error       = line.substr(6); }
        else if (line == "MAP_START")                 { inMap     = true; }
        else if (line == "MAP_END")                   { inMap     = false; }
        else if (line == "CONTENT_START")             { inContent = true; }
        else if (line == "CONTENT_END")               { inContent = false; }
        else if (inMap)                               { cur.mapText     += line + "\n"; }
        else if (inContent)                           { cur.contentText += line + "\n"; }
    }
    if (hasCur) results.push_back(cur);
    return results;
}

// ============================================================
//  Worker B
// ============================================================

static PageAnalysis analysePage(const std::string& url, const std::string& baseURL) {
    PageAnalysis pa;
    pa.uri = urlPath(url);

    std::string html = utils::downloadHTML(url);
    if (html.empty()) return pa;

    pa.imageCount = countTag(html, "<img ");
    pa.linkCount  = countTag(html, "<a ");
    pa.formCount  = countTag(html, "<form ");
    pa.headings   = extractHeadings(html);

    std::set<std::string> seen;
    for (const auto& href : extractHrefs(html)) {
        std::string resolved = resolveURL(href, url, baseURL);
        if (!resolved.empty() && seen.insert(resolved).second)
            pa.foundURLs.push_back(resolved);
    }
    return pa;
}

void runWorkerB(int rank, int N, int M) {
    // Determine the Worker A rank that owns this Worker B
    int workerArank = ((rank - N - 1) / M) + 1;

    while (true) {
        int tag = 0;
        std::string msg = mpiRecvStr(workerArank, MPI_ANY_TAG, nullptr, &tag);
        if (tag == TAG_TERMINATE) break;
        if (tag != TAG_TASK) continue;

        // msg = "<baseURL>\n<currentURL>"
        std::istringstream iss(msg);
        std::string baseURL, currentURL;
        std::getline(iss, baseURL);  baseURL    = trimCR(baseURL);
        std::getline(iss, currentURL); currentURL = trimCR(currentURL);

        PageAnalysis pa = analysePage(currentURL, baseURL);
        mpiSendStr(serializePageAnalysis(pa), workerArank, TAG_RESULT);
    }
}

// ============================================================
//  Worker A — domain crawl loop
// ============================================================

static std::string buildMapText(const std::map<std::string, PageAnalysis>& pages,
                                const std::set<std::pair<std::string,std::string>>& edges) {
    std::ostringstream oss;
    for (const auto& [uri, _] : pages)
        oss << "\"" << uri << "\"\n";
    for (const auto& [from, to] : edges)
        oss << "\"" << from << "\" \"" << to << "\"\n";
    return oss.str();
}

static std::string buildContentText(const std::map<std::string, PageAnalysis>& pages) {
    std::ostringstream oss;
    for (const auto& [uri, pa] : pages) {
        oss << "\"" << uri << "\"\n"
            << "IMAGES " << pa.imageCount << "\n"
            << "LINKS "  << pa.linkCount  << "\n"
            << "FORMS "  << pa.formCount  << "\n";
        for (const auto& [lvl, txt] : pa.headings) {
            for (int i = 0; i < lvl; i++) oss << '-';
            oss << " " << txt << "\n";
        }
        oss << "\n";
    }
    return oss.str();
}

static CrawlResult crawlDomain(const std::string& baseURL,
                                const std::vector<int>& workerBRanks) {
    CrawlResult result;
    result.baseURL   = baseURL;
    result.startTime = currentTimestamp("%Y-%m-%d %H:%M:%S");

    std::map<std::string, PageAnalysis>      pages;
    std::set<std::pair<std::string,std::string>> edges;
    std::set<std::string>  visited;
    std::deque<std::string> queue;

    visited.insert(baseURL);
    queue.push_back(baseURL);

    std::deque<int>      idle(workerBRanks.begin(), workerBRanks.end());
    std::map<int,std::string> busy; // worker rank → URL it is processing

    while (!queue.empty() || !busy.empty()) {
        // Dispatch to idle workers
        while (!idle.empty() && !queue.empty()) {
            int w = idle.front(); idle.pop_front();
            std::string url = queue.front(); queue.pop_front();
            mpiSendStr(baseURL + "\n" + url, w, TAG_TASK);
            busy[w] = url;
        }

        if (busy.empty()) continue;

        // Wait for any result from a Worker B
        int srcRank = 0;
        std::string msg = mpiRecvStr(MPI_ANY_SOURCE, TAG_RESULT, &srcRank);

        std::string processedURL = busy[srcRank];
        busy.erase(srcRank);
        idle.push_back(srcRank);

        PageAnalysis pa = deserializePageAnalysis(msg);
        pages[pa.uri] = pa;

        std::string fromURI = urlPath(processedURL);
        for (const auto& found : pa.foundURLs) {
            std::string toURI = urlPath(found);
            if (fromURI != toURI) edges.insert({fromURI, toURI});
            if (visited.insert(found).second) queue.push_back(found);
        }
    }

    result.mapText     = buildMapText(pages, edges);
    result.contentText = buildContentText(pages);
    result.endTime     = currentTimestamp("%Y-%m-%d %H:%M:%S");
    result.ok          = true;
    return result;
}

void runWorkerA(int rank, int N, int M) {
    // Worker B ranks assigned to this Worker A
    std::vector<int> myWorkerBs;
    for (int i = 0; i < M; i++)
        myWorkerBs.push_back(N + (rank - 1) * M + 1 + i);

    while (true) {
        int tag = 0;
        std::string msg = mpiRecvStr(0, MPI_ANY_TAG, nullptr, &tag);

        if (tag == TAG_TERMINATE) {
            for (int wb : myWorkerBs) mpiSendStr("", wb, TAG_TERMINATE);
            break;
        }
        if (tag != TAG_WORK) continue;

        // Parse newline-separated URLs
        std::vector<std::string> urls;
        std::istringstream iss(msg);
        std::string line;
        while (std::getline(iss, line)) {
            line = trimCR(line);
            if (!line.empty()) urls.push_back(line);
        }

        std::vector<CrawlResult> results;
        for (const auto& url : urls)
            results.push_back(crawlDomain(url, myWorkerBs));

        mpiSendStr(serializeCrawlResults(results), 0, TAG_DONE);
    }
}

// ============================================================
//  Master
// ============================================================

static void saveResults(const std::string& dirPath, const CrawlResult& r) {
    std::filesystem::create_directories(dirPath);

    { std::ofstream f(dirPath + "/map.txt");     f << r.mapText;     }
    { std::ofstream f(dirPath + "/content.txt"); f << r.contentText; }
    {
        std::ofstream f(dirPath + "/log.txt");
        f << r.startTime << "\n" << r.endTime << "\n";
        f << (r.ok ? "OK" : r.error) << "\n";
    }
}

static void masterProcess(const std::vector<std::string>& URLs, int N, int M,
                           std::string& output) {
    // Round-robin distribution
    std::vector<std::vector<std::string>> workloads(N + 1);
    for (size_t i = 0; i < URLs.size(); i++)
        workloads[(i % N) + 1].push_back(URLs[i]);

    // Send work to every Worker A (empty message = no work)
    for (int r = 1; r <= N; r++) {
        std::string msg;
        for (const auto& u : workloads[r]) msg += u + "\n";
        mpiSendStr(msg, r, TAG_WORK);
    }

    // Collect results
    std::vector<CrawlResult> all;
    for (int r = 1; r <= N; r++) {
        auto results = deserializeCrawlResults(mpiRecvStr(r, TAG_DONE));
        for (auto& res : results) all.push_back(std::move(res));
    }

    // Save to disk and build response HTML
    std::filesystem::create_directories("results");
    std::string dirTS = currentTimestamp("%Y_%m_%d_%H_%M");

    output = "<ul>\n";
    for (const auto& res : all) {
        std::string dirName = dirTS + "_" + urlToSafeName(res.baseURL);
        std::string dirPath = "results/" + dirName;
        saveResults(dirPath, res);
        output += "<li><strong>" + res.baseURL + "</strong> &rarr; "
               +  "<code>" + dirPath + "</code> "
               +  (res.ok ? "[OK]" : "[CHYBA: " + res.error + "]")
               +  "</li>\n";
    }
    if (all.empty()) output += "<li>Zadne URL nebyly zpracovany.</li>\n";
    output += "</ul>\n";
}

void runMaster(int N, int M) {
    CServer svr;
    if (!svr.Init("./data", "0.0.0.0", 8001)) {
        std::cerr << "Nelze inicializovat server!" << std::endl;
        return;
    }

    svr.RegisterFormCallback([N, M](const std::vector<std::string>& URLs, std::string& out) {
        masterProcess(URLs, N, M, out);
    });

    svr.Run();

    // Graceful shutdown — tell Worker As (which will cascade to Worker Bs)
    for (int r = 1; r <= N; r++) mpiSendStr("", r, TAG_TERMINATE);
}
