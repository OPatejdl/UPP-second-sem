#include "crawler.hpp"
#include "mpi_helpers.hpp"
#include "html_parser.hpp"
#include "url_utils.hpp"
#include "server.h"

#include <mpi.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <deque>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iostream>

// ============================================================
//  CrawlResult — aggregated output of one domain crawl
// ============================================================

struct CrawlResult {
    std::string base_url;
    std::string start_time;
    std::string end_time;
    std::string map_text;
    std::string content_text;
    bool        ok = true;
    std::string error;
};

static std::string serialize_crawl_results(const std::vector<CrawlResult>& results) {
    std::ostringstream oss;
    for (const auto& r : results) {
        oss << "BASEURL:"  << r.base_url   << "\n"
            << "START:"    << r.start_time << "\n"
            << "END:"      << r.end_time   << "\n"
            << "OK:"       << (r.ok ? "1" : "0") << "\n";
        if (!r.ok) oss << "ERROR:" << r.error << "\n";
        oss << "MAP_START\n"     << r.map_text     << "MAP_END\n"
            << "CONTENT_START\n" << r.content_text << "CONTENT_END\n";
    }
    return oss.str();
}

static std::vector<CrawlResult> deserialize_crawl_results(const std::string& data) {
    std::vector<CrawlResult> results;
    CrawlResult cur;
    bool has_cur = false, in_map = false, in_content = false;

    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim_cr(line);
        if (line.substr(0, 8) == "BASEURL:") {
            if (has_cur) results.push_back(cur);
            cur = {}; cur.base_url = line.substr(8);
            has_cur = true; in_map = in_content = false;
        }
        else if (line.substr(0, 6) == "START:")   cur.start_time   = line.substr(6);
        else if (line.substr(0, 4) == "END:")      cur.end_time     = line.substr(4);
        else if (line.substr(0, 3) == "OK:")       cur.ok           = (line[3] == '1');
        else if (line.substr(0, 6) == "ERROR:")    cur.error        = line.substr(6);
        else if (line == "MAP_START")              in_map     = true;
        else if (line == "MAP_END")                in_map     = false;
        else if (line == "CONTENT_START")          in_content = true;
        else if (line == "CONTENT_END")            in_content = false;
        else if (in_map)     cur.map_text     += line + "\n";
        else if (in_content) cur.content_text += line + "\n";
    }
    if (has_cur) results.push_back(cur);
    return results;
}

// ============================================================
//  Worker B
// ============================================================

void run_worker_b(int rank, int n, int m) {
    int worker_a_rank = ((rank - n - 1) / m) + 1;

    while (true) {
        int tag = 0;
        std::string msg = mpi_recv_str(worker_a_rank, MPI_ANY_TAG, nullptr, &tag);
        if (tag == TAG_TERMINATE) break;
        if (tag != TAG_TASK) continue;

        std::istringstream iss(msg);
        std::string base_url, current_url;
        std::getline(iss, base_url);    base_url    = trim_cr(base_url);
        std::getline(iss, current_url); current_url = trim_cr(current_url);

        mpi_send_str(serialize_page_analysis(analyse_page(current_url, base_url)),
                     worker_a_rank, TAG_RESULT);
    }
}

// ============================================================
//  Worker A
// ============================================================

static std::string build_map_text(const std::map<std::string, PageAnalysis>& pages,
                                   const std::set<std::pair<std::string, std::string>>& edges) {
    std::ostringstream oss;
    for (const auto& [uri, _] : pages) oss << "\"" << uri << "\"\n";
    for (const auto& [f, t]   : edges) oss << "\"" << f << "\" \"" << t << "\"\n";
    return oss.str();
}

static std::string build_content_text(const std::map<std::string, PageAnalysis>& pages) {
    std::ostringstream oss;
    for (const auto& [uri, pa] : pages) {
        oss << "\"" << uri << "\"\n"
            << "IMAGES " << pa.image_count << "\n"
            << "LINKS "  << pa.link_count  << "\n"
            << "FORMS "  << pa.form_count  << "\n";
        for (const auto& [lvl, txt] : pa.headings) {
            for (int i = 0; i < lvl; i++) oss << '-';
            oss << " " << txt << "\n";
        }
        oss << "\n";
    }
    return oss.str();
}

static CrawlResult crawl_domain(const std::string& base_url,
                                 const std::vector<int>& worker_b_ranks) {
    CrawlResult result;
    result.base_url   = base_url;
    result.start_time = current_timestamp("%Y-%m-%d %H:%M:%S");

    std::map<std::string, PageAnalysis>          pages;
    std::set<std::pair<std::string, std::string>> edges;
    std::set<std::string>   visited;
    std::deque<std::string> queue;
    std::deque<int>         idle(worker_b_ranks.begin(), worker_b_ranks.end());
    std::map<int, std::string> busy; // worker rank -> URL being processed

    visited.insert(base_url);
    queue.push_back(base_url);

    while (!queue.empty() || !busy.empty()) {
        while (!idle.empty() && !queue.empty()) {
            int w = idle.front(); idle.pop_front();
            std::string url = queue.front(); queue.pop_front();
            mpi_send_str(base_url + "\n" + url, w, TAG_TASK);
            busy[w] = url;
        }

        if (busy.empty()) continue;

        int src_rank = 0;
        PageAnalysis pa = deserialize_page_analysis(
            mpi_recv_str(MPI_ANY_SOURCE, TAG_RESULT, &src_rank));

        std::string processed_url = busy[src_rank];
        busy.erase(src_rank);
        idle.push_back(src_rank);

        pages[pa.uri] = pa;

        std::string from_uri = url_path(processed_url);
        for (const auto& found : pa.found_urls) {
            std::string to_uri = url_path(found);
            if (from_uri != to_uri) edges.insert({from_uri, to_uri});
            if (visited.insert(found).second) queue.push_back(found);
        }
    }

    result.map_text     = build_map_text(pages, edges);
    result.content_text = build_content_text(pages);
    result.end_time     = current_timestamp("%Y-%m-%d %H:%M:%S");
    return result;
}

void run_worker_a(int rank, int n, int m) {
    std::vector<int> my_worker_bs;
    for (int i = 0; i < m; i++)
        my_worker_bs.push_back(n + (rank - 1) * m + 1 + i);

    while (true) {
        int tag = 0;
        std::string msg = mpi_recv_str(0, MPI_ANY_TAG, nullptr, &tag);

        if (tag == TAG_TERMINATE) {
            for (int wb : my_worker_bs) mpi_send_str("", wb, TAG_TERMINATE);
            break;
        }
        if (tag != TAG_WORK) continue;

        std::vector<std::string> urls;
        std::istringstream iss(msg);
        std::string line;
        while (std::getline(iss, line)) {
            line = trim_cr(line);
            if (!line.empty()) urls.push_back(line);
        }

        std::vector<CrawlResult> results;
        for (const auto& url : urls)
            results.push_back(crawl_domain(url, my_worker_bs));

        mpi_send_str(serialize_crawl_results(results), 0, TAG_DONE);
    }
}

// ============================================================
//  Master
// ============================================================

static void save_results(const std::string& dir, const CrawlResult& r) {
    std::filesystem::create_directories(dir);
    { std::ofstream f(dir + "/map.txt");     f << r.map_text;     }
    { std::ofstream f(dir + "/content.txt"); f << r.content_text; }
    { std::ofstream f(dir + "/log.txt");
      f << r.start_time << "\n" << r.end_time << "\n"
        << (r.ok ? "OK" : r.error) << "\n"; }
}

static void master_process(const std::vector<std::string>& urls, int n, int m,
                            std::string& output) {
    // Distribute URLs round-robin across Worker A nodes
    std::vector<std::vector<std::string>> workloads(n + 1);
    for (size_t i = 0; i < urls.size(); i++)
        workloads[(i % n) + 1].push_back(urls[i]);

    for (int r = 1; r <= n; r++) {
        std::string msg;
        for (const auto& u : workloads[r]) msg += u + "\n";
        mpi_send_str(msg, r, TAG_WORK);
    }

    std::filesystem::create_directories("results");
    std::string dir_ts = current_timestamp("%Y_%m_%d_%H_%M");

    std::vector<CrawlResult> all;
    for (int r = 1; r <= n; r++)
        for (auto& res : deserialize_crawl_results(mpi_recv_str(r, TAG_DONE)))
            all.push_back(std::move(res));

    output = "<ul>\n";
    for (const auto& res : all) {
        std::string dir_path = "results/" + dir_ts + "_" + url_to_safe_name(res.base_url);
        save_results(dir_path, res);
        output += "<li><strong>" + res.base_url + "</strong> &rarr; <code>"
               + dir_path + "</code> "
               + (res.ok ? "[OK]" : "[ERROR: " + res.error + "]")
               + "</li>\n";
    }
    if (all.empty()) output += "<li>No URLs were processed.</li>\n";
    output += "</ul>\n";
}

void run_master(int n, int m) {
    CServer svr;
    if (!svr.Init("./data", "0.0.0.0", 8001)) {
        std::cerr << "Failed to initialise server\n";
        return;
    }

    svr.RegisterFormCallback([n, m](const std::vector<std::string>& urls, std::string& out) {
        master_process(urls, n, m, out);
    });

    svr.Run();

    // Cascade shutdown: master -> Worker As -> Worker Bs
    for (int r = 1; r <= n; r++) mpi_send_str("", r, TAG_TERMINATE);
}
