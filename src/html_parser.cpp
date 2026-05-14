#include "html_parser.hpp"
#include "url_utils.hpp"
#include "utils.h"

#include <sstream>
#include <set>
#include <algorithm>

// ── private helpers ──────────────────────────────────────────────────────────

static int count_tag(const std::string& html, const std::string& tag) {
    int n = 0;
    for (size_t p = 0; (p = html.find(tag, p)) != std::string::npos; p += tag.size())
        ++n;
    return n;
}

static std::string strip_tags(const std::string& s) {
    std::string r;
    bool in = false;
    for (char c : s) {
        if      (c == '<') in = true;
        else if (c == '>') in = false;
        else if (!in)      r += c;
    }
    return r;
}

static std::string trim(const std::string& s) {
    const auto ws = " \t\r\n";
    size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

static std::vector<std::string> extract_hrefs(const std::string& html) {
    std::vector<std::string> hrefs;
    for (size_t pos = 0; (pos = html.find("<a ", pos)) != std::string::npos; ) {
        size_t tag_end  = html.find('>', pos);
        if (tag_end == std::string::npos) break;
        size_t href_pos = html.find("href=", pos);
        if (href_pos == std::string::npos || href_pos > tag_end) { pos++; continue; }
        href_pos += 5;
        char q = html[href_pos];
        if (q != '"' && q != '\'') { pos++; continue; }
        size_t val_end = html.find(q, href_pos + 1);
        if (val_end == std::string::npos || val_end > tag_end) { pos++; continue; }
        hrefs.push_back(html.substr(href_pos + 1, val_end - href_pos - 1));
        pos = tag_end + 1;
    }
    return hrefs;
}

static std::vector<std::pair<int, std::string>> extract_headings(const std::string& html) {
    struct Entry { size_t pos; int lvl; std::string text; };
    std::vector<Entry> entries;

    for (int lvl = 1; lvl <= 6; lvl++) {
        std::string open  = "<h" + std::to_string(lvl);
        std::string close = "</h" + std::to_string(lvl) + ">";
        for (size_t pos = 0; ; ) {
            pos = html.find(open, pos);
            if (pos == std::string::npos) break;
            size_t gt = html.find('>', pos);
            if (gt == std::string::npos) break;
            size_t cl = html.find(close, gt);
            if (cl == std::string::npos) break;
            entries.push_back({pos, lvl, trim(strip_tags(html.substr(gt + 1, cl - gt - 1)))});
            pos = cl + close.size();
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.pos < b.pos; });

    std::vector<std::pair<int, std::string>> result;
    for (const auto& e : entries) result.push_back({e.lvl, e.text});
    return result;
}

// ── public API ────────────────────────────────────────────────────────────────

PageAnalysis analyse_page(const std::string& url, const std::string& base_url) {
    PageAnalysis pa;
    pa.uri = url_path(url);

    std::string html = utils::downloadHTML(url);
    if (html.empty()) return pa;

    pa.image_count = count_tag(html, "<img ");
    pa.link_count  = count_tag(html, "<a ");
    pa.form_count  = count_tag(html, "<form ");
    pa.headings    = extract_headings(html);

    std::set<std::string> seen;
    for (const auto& href : extract_hrefs(html)) {
        std::string resolved = resolve_url(href, url, base_url);
        if (!resolved.empty() && seen.insert(resolved).second)
            pa.found_urls.push_back(resolved);
    }
    return pa;
}

std::string serialize_page_analysis(const PageAnalysis& pa) {
    std::ostringstream oss;
    oss << "URI:"    << pa.uri         << "\n"
        << "IMAGES:" << pa.image_count << "\n"
        << "LINKS:"  << pa.link_count  << "\n"
        << "FORMS:"  << pa.form_count  << "\n";
    for (const auto& [lvl, txt] : pa.headings)
        oss << "HEADING:" << lvl << ":" << txt << "\n";
    for (const auto& u : pa.found_urls)
        oss << "FOUNDURL:" << u << "\n";
    return oss.str();
}

PageAnalysis deserialize_page_analysis(const std::string& data) {
    PageAnalysis pa;
    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim_cr(line);
        if      (line.substr(0, 4) == "URI:")      pa.uri         = line.substr(4);
        else if (line.substr(0, 7) == "IMAGES:")   pa.image_count = std::stoi(line.substr(7));
        else if (line.substr(0, 6) == "LINKS:")    pa.link_count  = std::stoi(line.substr(6));
        else if (line.substr(0, 6) == "FORMS:")    pa.form_count  = std::stoi(line.substr(6));
        else if (line.substr(0, 8) == "HEADING:") {
            auto rest = line.substr(8);
            size_t c = rest.find(':');
            if (c != std::string::npos)
                pa.headings.push_back({std::stoi(rest.substr(0, c)), rest.substr(c + 1)});
        }
        else if (line.substr(0, 9) == "FOUNDURL:") pa.found_urls.push_back(line.substr(9));
    }
    return pa;
}
