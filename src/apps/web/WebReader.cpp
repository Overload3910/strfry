#include "WebServer.h"
#include "WebData.h"





std::string exportUserEvents(lmdb::txn &txn, Decompressor &decomp, std::string_view pubkey) {
    std::string output;

    env.generic_foreachFull(txn, env.dbi_Event__pubkey, makeKey_StringUint64(pubkey, MAX_U64), "", [&](std::string_view k, std::string_view v){
        ParsedKey_StringUint64 parsedKey(k);
        if (parsedKey.s != pubkey) return false;

        uint64_t levId = lmdb::from_sv<uint64_t>(v);
        output += getEventJson(txn, decomp, levId);
        output += "\n";

        return true;
    }, true);

    return output;
}


std::string exportEventThread(lmdb::txn &txn, Decompressor &decomp, std::string_view rootId) {
    std::string output;

    {
        auto rootEv = lookupEventById(txn, rootId);
        if (rootEv) {
            output += getEventJson(txn, decomp, rootEv->primaryKeyId);
            output += "\n";
        }
    }

    std::string prefix = "e";
    prefix += rootId;

    env.generic_foreachFull(txn, env.dbi_Event__tag, prefix, "", [&](std::string_view k, std::string_view v){
        ParsedKey_StringUint64 parsedKey(k);
        if (parsedKey.s != prefix) return false;

        auto levId = lmdb::from_sv<uint64_t>(v);

        output += getEventJson(txn, decomp, levId);
        output += "\n";

        return true;
    });

    return output;
}

void doSearch(lmdb::txn &txn, Decompressor &decomp, std::string_view search, std::vector<TemplarResult> &results) {
    auto doesPubkeyExist = [&](std::string_view pubkey){
        bool ret = false;

        env.generic_foreachFull(txn, env.dbi_Event__pubkey, makeKey_StringUint64(pubkey, 0), "", [&](std::string_view k, std::string_view v){
            ParsedKey_StringUint64 parsedKey(k);

            if (parsedKey.s == pubkey) ret = true;

            return false;
        });

        return ret;
    };

    if (search.size() == 64) {
        try {
            auto word = from_hex(search);
            if (doesPubkeyExist(word)) results.emplace_back(tmpl::search::userResult(User(txn, decomp, word)));
        } catch(...) {}
    }

    if (search.starts_with("npub1")) {
        try {
            auto word = decodeBech32Simple(search);
            if (doesPubkeyExist(word)) results.emplace_back(tmpl::search::userResult(User(txn, decomp, word)));
        } catch(...) {}
    }

    try {
        auto e = Event::fromIdExternal(txn, search);
        results.emplace_back(tmpl::search::eventResult(e));
    } catch(...) {}
}





TemplarResult renderCommunityEvents(lmdb::txn &txn, Decompressor &decomp, UserCache &userCache, const CommunitySpec &communitySpec) {
    AlgoScanner a(txn, communitySpec.algo);
    auto events = a.getEvents(txn, decomp, 300);

    std::vector<TemplarResult> rendered;
    auto now = hoytech::curr_time_s();
    uint64_t n = 1;

    for (auto &fe : events) {
        auto ev = Event::fromLevId(txn, fe.levId);
        ev.populateJson(txn, decomp);

        struct {
            uint64_t n;
            const Event &ev;
            const User &user;
            std::string timestamp;
            AlgoScanner::EventInfo &info;
        } ctx = {
            n,
            ev,
            *userCache.getUser(txn, decomp, ev.getPubkey()),
            renderTimestamp(now, ev.getCreatedAt()),
            fe.info,
        };

        rendered.emplace_back(tmpl::community::item(ctx));
        n++;
    }

    return tmpl::community::list(rendered);
}






void WebServer::handleReadRequest(lmdb::txn &txn, Decompressor &decomp, const MsgWebReader::Request *msg) {
    auto startTime = hoytech::curr_time_us();
    const auto &req = msg->req;
    Url u(req.url);

    LI << "READ REQUEST: " << req.url;

    UserCache userCache;

    std::optional<std::string> rawBody;

    std::string_view code = "200 OK";
    std::string_view contentType = "text/html; charset=utf-8";
    std::optional<TemplarResult> body;
    std::optional<CommunitySpec> communitySpec;
    std::string title;

    if (u.path.size() == 0 || u.path[0] == "algo") {
        communitySpec = lookupCommunitySpec(txn, decomp, userCache, cfg().web__homepageCommunity);
    }

    if (u.path.size() == 0) {
        body = renderCommunityEvents(txn, decomp, userCache, *communitySpec);
    } else if (u.path[0] == "algo") {
        struct {
            std::string community;
            const CommunitySpec &communitySpec;
            std::string_view descriptor;
        } ctx = {
            "homepage",
            *communitySpec,
            cfg().web__homepageCommunity,
        };

        body = tmpl::community::communityInfo(ctx);
    } else if (u.path[0] == "e") {
        if (u.path.size() == 2) {
            EventThread et(txn, decomp, decodeBech32Simple(u.path[1]));
            body = et.render(txn, decomp, userCache);
        } else if (u.path.size() == 3) {
            if (u.path[2] == "raw.json") {
                auto ev = Event::fromIdExternal(txn, u.path[1]);
                ev.populateJson(txn, decomp);
                rawBody = tao::json::to_string(ev.json, 4);
                contentType = "application/json; charset=utf-8";
            } else if (u.path[2] == "export.jsonl") {
                rawBody = exportEventThread(txn, decomp, decodeBech32Simple(u.path[1]));
                contentType = "application/jsonl+json; charset=utf-8";
            }
        }
    } else if (u.path[0] == "u") {
        if (u.path.size() == 2) {
            User user(txn, decomp, decodeBech32Simple(u.path[1]));
            title = std::string("profile: ") + user.username;
            body = tmpl::user::metadata(user);
        } else if (u.path.size() == 3) {
            std::string userPubkey;

            if (u.path[1].starts_with("npub1")) {
                userPubkey = decodeBech32Simple(u.path[1]);
            } else {
                userPubkey = from_hex(u.path[1]);
            }

            if (u.path[2] == "notes") {
                UserEvents uc(txn, decomp, userPubkey);
                title = std::string("notes: ") + uc.u.username;
                body = uc.render(txn, decomp);
            } else if (u.path[2] == "export.jsonl") {
                rawBody = exportUserEvents(txn, decomp, userPubkey);
                contentType = "application/jsonl+json; charset=utf-8";
            } else if (u.path[2] == "metadata.json") {
                User user(txn, decomp, userPubkey);
                rawBody = user.kind0Found() ? tao::json::to_string(*user.kind0Json) : "{}";
                contentType = "application/json; charset=utf-8";
            } else if (u.path[2] == "following") {
                User user(txn, decomp, userPubkey);
                title = std::string("following: ") + user.username;
                user.populateContactList(txn, decomp);

                struct {
                    User &user;
                    std::function<const User*(const std::string &)> getUser;
                } ctx = {
                    user,
                    [&](const std::string &pubkey){ return userCache.getUser(txn, decomp, pubkey); },
                };

                body = tmpl::user::following(ctx);
            } else if (u.path[2] == "followers") {
                User user(txn, decomp, userPubkey);
                title = std::string("followers: ") + user.username;
                auto followers = user.getFollowers(txn, decomp, user.pubkey);

                struct {
                    const User &user;
                    const std::vector<std::string> &followers;
                    std::function<const User*(const std::string &)> getUser;
                } ctx = {
                    user,
                    followers,
                    [&](const std::string &pubkey){ return userCache.getUser(txn, decomp, pubkey); },
                };

                body = tmpl::user::followers(ctx);
            }
        }
    } else if (u.path[0] == "search") {
        std::vector<TemplarResult> results;

        if (u.query.starts_with("q=")) {
            std::string_view search = u.query.substr(2);

            doSearch(txn, decomp, search, results);

            struct {
                std::string_view search;
                const std::vector<TemplarResult> &results;
            } ctx = {
                search,
                results,
            };

            body = tmpl::searchPage(ctx);
        }
    } else if (u.path[0] == "post") {
        body = tmpl::newPost(nullptr);
    }




    std::string responseData;

    if (body) {
        if (title.size()) title += " | ";

        struct {
            const TemplarResult &body;
            const std::optional<CommunitySpec> &communitySpec;
            std::string_view title;
            std::string staticFilesPrefix;
        } ctx = {
            *body,
            communitySpec,
            title,
            "http://127.0.0.1:8081",
        };

        responseData = std::move(tmpl::main(ctx).str);
    } else if (rawBody) {
        responseData = std::move(*rawBody);
    } else {
        code = "404 Not Found";
        body = TemplarResult{ "Not found" };
    }

    LI << "Reply: " << code << " / " << responseData.size() << " bytes in " << (hoytech::curr_time_us() - startTime) << "us";
    sendHttpResponseAndUnlock(msg->lockedThreadId, req, responseData, code, contentType);
}



void WebServer::runReader(ThreadPool<MsgWebReader>::Thread &thr) {
    Decompressor decomp;

    while(1) {
        auto newMsgs = thr.inbox.pop_all();

        auto txn = env.txn_ro();

        for (auto &newMsg : newMsgs) {
            if (auto msg = std::get_if<MsgWebReader::Request>(&newMsg.msg)) {
                try {
                    handleReadRequest(txn, decomp, msg);
                } catch (std::exception &e) {
                    sendHttpResponseAndUnlock(msg->lockedThreadId, msg->req, "Server error", "500 Server Error");
                    LE << "500 server error: " << e.what();
                }
            }
        }
    }
}
