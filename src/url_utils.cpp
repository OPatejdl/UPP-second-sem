#include "url_utils.hpp"

#include <sstream>
#include <vector>
#include <chrono>
#include <ctime>

std::string scheme_and_domain(const std::string& url) {
    size_t pos = url.find("://");
    if (pos == std::string::npos) return "";
    size_t path_start = url.find('/', pos + 3);
    return (path_start == std::string::npos) ? url : url.substr(0, path_start);
}

std::string url_path(const std::string& url) {
    size_t pos = url.find("://");
    if (pos == std::string::npos) return "/";
    size_t path_start = url.find('/', pos + 3);
    if (path_start == std::string::npos) return "/";
    std::string p = url.substr(path_start);
    size_t frag = p.find('#'); if (frag != std::string::npos) p = p.substr(0, frag);
    size_t qry  = p.find('?'); if (qry  != std::string::npos) p = p.substr(0, qry);
    return p;
}

// Resolves ".." and "." segments in a path.
static std::string normalize_path(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if      (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (seg != "." && !seg.empty()) parts.push_back(seg);
    }
    if (parts.empty()) return "/";
    std::string r;
    for (const auto& p : parts) r += "/" + p;
    return r;
}

bool in_scope(const std::string& url, const std::string& base_url) {
    if (url.size() < base_url.size()) return false;
    if (url.substr(0, base_url.size()) != base_url) return false;
    if (url.size() == base_url.size()) return true;
    if (base_url.back() == '/') return true;
    return url[base_url.size()] == '/';
}

std::string resolve_url(const std::string& href,
                        const std::string& current_url,
                        const std::string& base_url) {
    if (href.empty() || href[0] == '#') return "";
    if (href.size() >= 11 && href.substr(0, 11) == "javascript:") return "";
    if (href.size() >=  7 && href.substr(0,  7) == "mailto:")     return "";

    std::string full;
    if      (href.size() >= 7 && href.substr(0, 7) == "http://")  full = href;
    else if (href.size() >= 8 && href.substr(0, 8) == "https://") full = href;
    else if (!href.empty() && href[0] == '/') {
        full = scheme_and_domain(base_url) + href;
    } else {
        std::string cur_p = url_path(current_url);
        std::string dir   = cur_p.substr(0, cur_p.rfind('/') + 1);
        full = scheme_and_domain(base_url) + dir + href;
    }

    full = scheme_and_domain(full) + normalize_path(url_path(full));
    return in_scope(full, base_url) ? full : "";
}

std::string url_to_safe_name(const std::string& url) {
    std::string s = url;
    size_t pos = s.find("://");
    if (pos != std::string::npos) s = s.substr(pos + 3);
    std::string r;
    for (char c : s) r += (c == '.' || c == '/') ? '_' : c;
    while (!r.empty() && r.back() == '_') r.pop_back();
    return r;
}

std::string current_timestamp(const char* fmt) {
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

std::string trim_cr(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}
