namespace GOTHIC_ENGINE {
    // Automated test bot. Off unless GothicCoopConfig.json has an "autotest" object:
    //   "autotest": { "role": "server" | "client",
    //                 "startAfterMs": 8000,      // after the world is loaded
    //                 "reportEveryMs": 1000,
    //                 "teleports": [ [40000, 600, 0, 0], ... ] }  // at ms after world load, move by dx,dy,dz
    // Writes one JSON object per line to GothicCoopTest.log next to the exe.
    // It calls the same StartCoopServer/StartCoopClient as the F1/F2 keys.

    static bool AutoTestEnabled = false;
    static bool AutoTestConfigRead = false;
    static std::string AutoTestRole;
    static long long AutoTestStartAfterMs = 8000;
    static long long AutoTestReportEveryMs = 1000;
    static long long AutoTestWorldMs = 0;
    static long long AutoTestLastReportMs = 0;
    static bool AutoTestSessionStarted = false;
    static std::vector<std::array<float, 4>> AutoTestTeleports;
    static size_t AutoTestNextTeleport = 0;
    static std::string AutoTestLogPath;

    static void AutoTestWrite(json& j) {
        j["t"] = CurrentMs - AutoTestWorldMs;
        j["wall"] = CurrentMs; // same clock for every copy on one PC
        j["nick"] = std::string(MyNickname.ToChar());
        j["role"] = AutoTestRole;
        std::ofstream f(AutoTestLogPath, std::ios_base::app | std::ios_base::out);
        f << j.dump() << "\n";
    }

    static json AutoTestPos(oCNpc* npc) {
        auto p = npc->GetPositionWorld();
        return json::array({ (int)p[0], (int)p[1], (int)p[2] });
    }

    static void AutoTestReadConfig() {
        AutoTestConfigRead = true;
        if (!CoopConfig.contains("autotest")) {
            return;
        }

        auto& c = CoopConfig["autotest"];
        AutoTestRole = c.value("role", std::string(""));
        if (AutoTestRole != "server" && AutoTestRole != "client") {
            return;
        }

        AutoTestStartAfterMs = c.value("startAfterMs", 8000);
        AutoTestReportEveryMs = c.value("reportEveryMs", 1000);
        if (c.contains("teleports")) {
            for (auto& tp : c["teleports"]) {
                AutoTestTeleports.push_back({ tp[0].get<float>(), tp[1].get<float>(), tp[2].get<float>(), tp[3].get<float>() });
            }
        }

        AutoTestLogPath = std::string(GothicExeFolderPath) + "\\GothicCoopTest.log";
        AutoTestEnabled = true;
    }

    // Frame profiler for the test log: Game_Loop calls AutoTestMark after each part;
    // a part over 200 ms, or a gap between two Game_Loop calls over 500 ms, is logged.
    static long long AutoTestLastMarkMs = 0;
    static long long AutoTestLastFrameMs = 0;

    void AutoTestMark(const char* part) {
        if (!AutoTestConfigRead) {
            AutoTestReadConfig();
        }
        if (!AutoTestEnabled) {
            return;
        }

        long long now = GetCurrentMs();
        if (part == NULL) {
            DiagLoopTick();
            if (AutoTestLastFrameMs && now - AutoTestLastFrameMs > 500 && AutoTestWorldMs) {
                json j; j["event"] = "slow_frame"; j["gapMs"] = now - AutoTestLastFrameMs;
                AutoTestWrite(j);
            }
            AutoTestLastFrameMs = now;
        }
        else if (now - AutoTestLastMarkMs > 200 && AutoTestWorldMs) {
            json j; j["event"] = "slow_part"; j["part"] = part; j["ms"] = now - AutoTestLastMarkMs;
            AutoTestWrite(j);
        }
        AutoTestLastMarkMs = now;
    }

    void AutoTestLoop() {
        if (!AutoTestConfigRead) {
            AutoTestReadConfig();
        }

        if (!AutoTestEnabled || !player || !ogame) {
            return;
        }

        if (AutoTestWorldMs == 0) {
            AutoTestWorldMs = CurrentMs;
            json j;
            j["event"] = "world_loaded";
            j["world"] = std::string(ogame->GetGameWorld()->GetWorldFilename().ToChar());
            j["me"] = AutoTestPos(player);
            AutoTestWrite(j);
        }

        auto elapsed = CurrentMs - AutoTestWorldMs;

        if (!AutoTestSessionStarted && elapsed >= AutoTestStartAfterMs && !ServerThread && !ClientThread) {
            AutoTestSessionStarted = true;
            if (AutoTestRole == "server") {
                StartCoopServer();
            }
            else {
                StartCoopClient();
            }

            json j;
            j["event"] = "session_started";
            AutoTestWrite(j);
        }

        if (AutoTestSessionStarted && AutoTestNextTeleport < AutoTestTeleports.size()
            && elapsed >= (long long)AutoTestTeleports[AutoTestNextTeleport][0]) {
            auto& tp = AutoTestTeleports[AutoTestNextTeleport++];
            auto p = player->GetPositionWorld();
            // without collision detection, or walls push the hero back to where it was
            player->SetCollDet(FALSE);
            player->SetPositionWorld(zVEC3(p[0] + tp[1], p[1] + tp[2], p[2] + tp[3]));
            player->SetCollDet(TRUE);

            json j;
            j["event"] = "teleport";
            j["me"] = AutoTestPos(player);
            AutoTestWrite(j);
        }

        if (elapsed - AutoTestLastReportMs >= AutoTestReportEveryMs) {
            AutoTestLastReportMs = elapsed;

            json j;
            j["event"] = "state";
            j["session"] = ServerThread ? "server" : (ClientThread ? "client" : "none");
            j["paused"] = IsCoopPaused;
            j["ping"] = CurrentPing;
            j["me"] = AutoTestPos(player);
            j["hp"] = player->GetAttribute(NPC_ATR_HITPOINTS);
            j["syncNpcs"] = (int)SyncNpcs.size();
            j["broadcastNpcs"] = (int)BroadcastNpcs.size();

            json players = json::array();
            for (auto& pn : PlayerNpcs) {
                json pj;
                pj["name"] = std::string(pn.second.ToChar());
                pj["pos"] = AutoTestPos(pn.first);
                pj["hp"] = pn.first->GetAttribute(NPC_ATR_HITPOINTS);
                players.push_back(pj);
            }
            j["players"] = players;
            AutoTestWrite(j);
        }
    }
}
