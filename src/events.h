#pragma once

#include <secp256k1_schnorrsig.h>

#include "golpe.h"

#include "Decompressor.h"




inline bool isDefaultReplaceableKind(uint64_t kind) {
    return (
        kind == 0 ||
        kind == 3 ||
        kind == 41 ||
        (kind >= 10'000 && kind < 20'000) ||
        (kind >= 30'000 && kind < 40'000)
    );
}

inline bool isDefaultEphemeralKind(uint64_t kind) {
    return (
        (kind >= 20'000 && kind < 30'000)
    );
}




std::string nostrJsonToFlat(const tao::json::value &v);
std::string nostrHash(const tao::json::value &origJson);

bool verifySig(secp256k1_context* ctx, std::string_view sig, std::string_view hash, std::string_view pubkey);
void verifyNostrEvent(secp256k1_context *secpCtx, const NostrIndex::Event *flat, const tao::json::value &origJson);
void verifyNostrEventJsonSize(std::string_view jsonStr);
void verifyEventTimestamp(const NostrIndex::Event *flat);

void parseAndVerifyEvent(const tao::json::value &origJson, secp256k1_context *secpCtx, bool verifyMsg, bool verifyTime, std::string &flatStr, std::string &jsonStr);


// Does not do verification!
inline const NostrIndex::Event *flatStrToFlatEvent(std::string_view flatStr) {
    return flatbuffers::GetRoot<NostrIndex::Event>(flatStr.data());
}


std::optional<defaultDb::environment::View_Event> lookupEventById(lmdb::txn &txn, std::string_view id);
defaultDb::environment::View_Event lookupEventByLevId(lmdb::txn &txn, uint64_t levId); // throws if can't find
uint64_t getMostRecentLevId(lmdb::txn &txn);
std::string_view decodeEventPayload(lmdb::txn &txn, Decompressor &decomp, std::string_view raw, uint32_t *outDictId, size_t *outCompressedSize);
std::string_view getEventJson(lmdb::txn &txn, Decompressor &decomp, uint64_t levId);
std::string_view getEventJson(lmdb::txn &txn, Decompressor &decomp, uint64_t levId, std::string_view eventPayload);

inline quadrable::Key flatEventToQuadrableKey(const NostrIndex::Event *flat) {
    uint64_t timestamp = flat->created_at();
    if (timestamp > MAX_TIMESTAMP) throw herr("timestamp is too large to encode in quadrable key");
    return quadrable::Key::fromIntegerAndHash(timestamp, sv(flat->id()).substr(0, 27));
}




enum class EventSourceType {
    None = 0,
    IP4 = 1,
    IP6 = 2,
    Import = 3,
    Stream = 4,
    Sync = 5,
};

inline std::string eventSourceTypeToStr(EventSourceType t) {
    if (t == EventSourceType::IP4) return "IP4";
    else if (t == EventSourceType::IP6) return "IP6";
    else if (t == EventSourceType::Import) return "Import";
    else if (t == EventSourceType::Stream) return "Stream";
    else if (t == EventSourceType::Sync) return "Sync";
    else return "?";
}



enum class EventWriteStatus {
    Pending,
    Written,
    Duplicate,
    Replaced,
    Deleted,
};


struct EventToWrite {
    std::string flatStr;
    std::string jsonStr;
    uint64_t receivedAt;
    EventSourceType sourceType;
    std::string sourceInfo;
    void *userData = nullptr;
    quadrable::Key quadKey;
    EventWriteStatus status = EventWriteStatus::Pending;
    uint64_t levId = 0;

    EventToWrite() {}

    EventToWrite(std::string flatStr, std::string jsonStr, uint64_t receivedAt, EventSourceType sourceType, std::string sourceInfo, void *userData = nullptr) : flatStr(flatStr), jsonStr(jsonStr), receivedAt(receivedAt), sourceType(sourceType), sourceInfo(sourceInfo), userData(userData) {
        const NostrIndex::Event *flat = flatbuffers::GetRoot<NostrIndex::Event>(flatStr.data());
        quadKey = flatEventToQuadrableKey(flat);
    }
};


void writeEvents(lmdb::txn &txn, quadrable::Quadrable &qdb, std::vector<EventToWrite> &evs, uint64_t logLevel = 1);
void deleteEvent(lmdb::txn &txn, quadrable::Quadrable::UpdateSet &changes, defaultDb::environment::View_Event &ev);
