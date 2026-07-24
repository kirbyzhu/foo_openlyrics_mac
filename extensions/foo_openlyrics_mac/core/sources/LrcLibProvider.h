#pragma once
#include "sources/LyricSource.h"
#include "model/SearchResult.h"
#include "ports/HttpClient.h"
#include <vector>

namespace openlyrics {

class LrcLibProvider : public LyricSource {
public:
    explicit LrcLibProvider(HttpClient& http);
    bool fetch(const TrackMeta& track, LyricData& out, CancelToken* cancel = nullptr) override;

    bool search(const std::string& query, std::vector<SearchResult>& out, CancelToken* cancel = nullptr);
    bool fetchById(int id, LyricData& out, CancelToken* cancel = nullptr);

    // LyricSource 接口适配
    bool search(const TrackMeta& track, std::vector<SearchResult>& out, CancelToken* cancel = nullptr) override;
    bool fetchById(const std::string& id, LyricData& out, CancelToken* cancel = nullptr) override;
    SourceId sourceId() const override { return SourceId::LrcLib; }

private:
    HttpClient& http_;
};

}  // namespace openlyrics
