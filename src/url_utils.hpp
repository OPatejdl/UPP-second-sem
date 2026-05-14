#pragma once

#include <string>

// Returns the "scheme://domain" prefix of a URL.
// url    - full URL
// returns - prefix such as "http://example.com", or "" on failure
std::string scheme_and_domain(const std::string& url);

// Returns the path component of a URL, stripping fragment and query string.
// url    - full URL
// returns - path such as "/portal/about"
std::string url_path(const std::string& url);

// Returns true if url falls within the crawl scope defined by base_url.
// url      - URL to check
// base_url - root URL of the crawl
// returns  - true if url starts with base_url and is followed by '/' or end of string
bool in_scope(const std::string& url, const std::string& base_url);

// Resolves href to a full URL relative to current_url and checks crawl scope.
// href        - value of an href attribute (relative or absolute)
// current_url - URL of the page where href was found
// base_url    - crawl root for scope filtering
// returns     - normalised full URL if in scope, "" otherwise
std::string resolve_url(const std::string& href,
                        const std::string& current_url,
                        const std::string& base_url);

// Converts a URL to a filesystem-safe name (strips scheme, replaces '.' and '/' with '_').
// url    - full URL
// returns - safe string such as "upp-test-1_martinubl_cz"
std::string url_to_safe_name(const std::string& url);

// Returns the current local time formatted with the given strftime pattern.
// fmt    - strftime pattern, e.g. "%Y-%m-%d %H:%M:%S"
// returns - formatted time string
std::string current_timestamp(const char* fmt);

// Removes a trailing '\r' from a string (Windows line-ending compatibility).
// s      - input string
// returns - string without trailing '\r'
std::string trim_cr(std::string s);
