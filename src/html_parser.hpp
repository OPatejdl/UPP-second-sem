#pragma once

#include <string>
#include <vector>
#include <utility>

struct PageAnalysis {
    std::string uri;
    int image_count = 0;
    int link_count  = 0;
    int form_count  = 0;
    std::vector<std::pair<int, std::string>> headings; // (level 1-6, text)
    std::vector<std::string> found_urls;               // in-scope full URLs
};

// Downloads and analyses a single HTML page.
// url      - full URL of the page to fetch
// base_url - crawl root; links outside this prefix are discarded
// returns  - PageAnalysis with tag counts, headings and filtered found URLs
PageAnalysis analyse_page(const std::string& url, const std::string& base_url);

// Serialises a PageAnalysis into a newline-delimited string for MPI transport.
// pa     - page analysis to serialise
// returns - serialised string
std::string serialize_page_analysis(const PageAnalysis& pa);

// Deserialises a PageAnalysis from a string produced by serialize_page_analysis.
// data   - serialised string
// returns - reconstructed PageAnalysis
PageAnalysis deserialize_page_analysis(const std::string& data);
