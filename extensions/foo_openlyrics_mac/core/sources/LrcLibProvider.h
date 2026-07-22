#pragma once
#include "sources/LyricSource.h"
#include "model/SearchResult.h"
#include "ports/HttpClient.h"
#include <vector>

namespace openlyrics {

class LrcLibProvider : public LyricSource {
public:
    explicit LrcLibProvider(HttpClient& http);
    bool fetch(const TrackMeta& track, LyricData& out) override;

    bool search(const std::string& query, std::vector<SearchResult>& out);
    bool fetchById(int id, LyricData& out);

private:
    HttpClient& http_;
};

}  // namespace openlyrics
