namespace GOTHIC_ENGINE {
    // Test-only probes for the shared-story research, driven by AutoTest. Keys in "autotest":
    //   "storyEveryMs": 10000     snapshot of story state (script ints, told infos, log, xp);
    //                             changes since the last snapshot go to GothicCoopTest.log,
    //                             the full state to GothicCoopStory.json
    //   "nearRadius": 3000        state lines list NPCs within this range (0 = off)
    //   "dumpNpcs": true          all NPCs with positions to GothicCoopNpcs.json at world load
    //   "talks": [[ms, "PC_PSIONIC", 3]]   at ms after world load: walk up to that NPC, start a
    //                             talk the way the game does (AssessTalk), pick up to 3 choices
    //                             (the first in the list each time), then leave

    static long long ProbeStoryEveryMs = 0;
    static long long ProbeLastStoryMs = -1;
    static int ProbeNearRadius = 0;
    static bool ProbeDumpNpcs = false;
    static bool ProbePauseDuringTalk = false;  // "pauseDuringTalk": press F2 (pause sync) around the talk
    static bool ProbePausedForTalk = false;
    static bool ProbeNpcsDumped = false;

    struct ProbeTalk { long long at; std::string npc; int choices; };
    static std::vector<ProbeTalk> ProbeTalks;
    static size_t ProbeNextTalk = 0;
    static bool ProbeTalkActive = false;
    static long long ProbeTalkStartMs = 0;
    static long long ProbeTalkLastChoiceMs = 0;
    static int ProbeTalkChoicesLeft = 0;
    static std::string ProbeTalkNpc;
    static oCNpc* ProbeTalkNpcPtr = NULL;

    static std::vector<zCPar_Symbol*> ProbeIntSymbols;
    static std::vector<int> ProbeIntValues;      // flattened, in ProbeIntSymbols order
    static std::vector<std::string> ProbeIntNames;
    static std::vector<int> ProbeTold;
    static std::vector<std::string> ProbeInfoNames;
    static int ProbeTopics = -1, ProbeEntries = -1;
    static bool ProbeHaveSnapshot = false;

    static int ProbeNearRadiusValue() {
        return ProbeNearRadius;
    }

    static void StoryProbeReadConfig(json& c) {
        ProbeStoryEveryMs = c.value("storyEveryMs", 0);
        ProbeNearRadius = c.value("nearRadius", 0);
        ProbeDumpNpcs = c.value("dumpNpcs", false);
        ProbePauseDuringTalk = c.value("pauseDuringTalk", false);
        if (c.contains("talks")) {
            for (auto& t : c["talks"]) {
                ProbeTalks.push_back({ t[0].get<long long>(), t[1].get<std::string>(), t.size() > 2 ? t[2].get<int>() : 3 });
            }
        }
    }

    static std::string ProbeUpper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return s;
    }

    // Global int variables of the game scripts (no constants, class fields or function locals).
    static void ProbeCollectSymbols() {
        auto& table = parser->symtab.table;
        for (int i = 0; i < table.GetNum(); i++) {
            auto sym = table[i];
            if (!sym || sym->type != zPAR_TYPE_INT || (sym->flags & (zPAR_FLAG_CONST | zPAR_FLAG_CLASSVAR | zPAR_FLAG_EXTERNAL))) {
                continue;
            }
            std::string name = sym->name.ToChar();
            if (name.find('.') != std::string::npos) {
                continue;
            }
            ProbeIntSymbols.push_back(sym);
            for (unsigned int e = 0; e < (sym->ele ? sym->ele : 1); e++) {
                ProbeIntNames.push_back(sym->ele > 1 ? name + "[" + std::to_string(e) + "]" : name);
            }
        }
    }

    static std::vector<int> ProbeReadInts() {
        std::vector<int> v;
        v.reserve(ProbeIntNames.size());
        for (auto sym : ProbeIntSymbols) {
            unsigned int n = sym->ele ? sym->ele : 1;
            for (unsigned int e = 0; e < n; e++) {
                v.push_back(n > 1 ? (sym->intdata ? sym->intdata[e] : 0) : sym->single_intdata);
            }
        }
        return v;
    }

    static void ProbeReadInfos(std::vector<int>& told, std::vector<std::string>* names) {
        auto im = ogame->GetInfoManager();
        if (!im) {
            return;
        }
        for (auto node = im->infoList.next; node; node = node->next) {
            if (!node->data) {
                continue;
            }
            told.push_back(node->data->told);
            if (names) {
                names->push_back(node->data->name.ToChar());
            }
        }
    }

    static void ProbeReadLog(int& topics, int& entries, json* titles) {
        topics = 0;
        entries = 0;
        auto& lm = oCLogManager::GetLogManager();
        for (auto node = lm.m_lstTopics.next; node; node = node->next) {
            if (!node->data) {
                continue;
            }
            topics++;
            entries += node->data->m_lstEntries.GetNumInList();
            if (titles) {
                titles->push_back({ std::string(node->data->m_strDescription.ToChar()), (int)node->data->m_enuStatus, node->data->m_lstEntries.GetNumInList() });
            }
        }
    }

    static void ProbeStorySnapshot() {
        if (ProbeIntSymbols.empty()) {
            ProbeCollectSymbols();
        }

        auto ints = ProbeReadInts();
        std::vector<int> told;
        std::vector<std::string> infoNames;
        ProbeReadInfos(told, &infoNames);
        int topics, entries;
        json titles = json::array();
        ProbeReadLog(topics, entries, &titles);

        json j;
        j["event"] = "story";
        j["ints"] = (int)ints.size();
        j["infos"] = (int)told.size();
        j["told"] = (int)std::count_if(told.begin(), told.end(), [](int t) { return t != 0; });
        j["topics"] = topics;
        j["entries"] = entries;
        j["xp"] = (long long)player->experience_points;
        j["lp"] = (long long)player->learn_points;
        j["level"] = player->level;

        if (ProbeHaveSnapshot) {
            json changed = json::array();
            int nChanged = 0;
            for (size_t i = 0; i < ints.size() && i < ProbeIntValues.size(); i++) {
                if (ints[i] != ProbeIntValues[i]) {
                    if (nChanged++ < 60) {
                        changed.push_back({ ProbeIntNames[i], ProbeIntValues[i], ints[i] });
                    }
                }
            }
            j["changedCount"] = nChanged;
            j["changed"] = changed;

            json newTold = json::array();
            for (size_t i = 0; i < told.size() && i < ProbeTold.size(); i++) {
                if (told[i] && !ProbeTold[i]) {
                    newTold.push_back(infoNames[i]);
                }
            }
            j["newTold"] = newTold;
            j["topicsDelta"] = topics - ProbeTopics;
            j["entriesDelta"] = entries - ProbeEntries;
        }
        AutoTestWrite(j);

        // full state, overwritten each time, for comparing the two copies afterwards
        json full;
        full["wall"] = CurrentMs;
        full["nick"] = std::string(MyNickname.ToChar());
        json intsObj = json::object();
        for (size_t i = 0; i < ints.size(); i++) {
            intsObj[ProbeIntNames[i]] = ints[i];
        }
        full["ints"] = intsObj;
        json toldNames = json::array();
        for (size_t i = 0; i < told.size(); i++) {
            if (told[i]) {
                toldNames.push_back(infoNames[i]);
            }
        }
        full["told"] = toldNames;
        full["log"] = titles;
        full["xp"] = (long long)player->experience_points;
        std::ofstream f(std::string(GothicExeFolderPath) + "\\GothicCoopStory.json", std::ios_base::trunc | std::ios_base::out);
        f << full.dump();

        ProbeIntValues = ints;
        ProbeTold = told;
        ProbeInfoNames = infoNames;
        ProbeTopics = topics;
        ProbeEntries = entries;
        ProbeHaveSnapshot = true;
    }

    static std::string ProbeActiveAni(oCNpc* npc) {
        auto model = npc->GetModel();
        if (!model || model->numActiveAnis <= 0) {
            return "";
        }
        auto active = model->aniChannels[0];
        if (!active || !active->protoAni) {
            return "";
        }
        return active->protoAni->aniName.ToChar();
    }

    static json StoryNearAround(zVEC3 me) {
        json list = json::array();
        if (ProbeNearRadius <= 0) {
            return list;
        }

        std::vector<std::pair<float, oCNpc*>> closeNpcs;
        for (auto node = ogame->GetGameWorld()->voblist_npcs->next; node; node = node->next) {
            auto npc = node->data;
            if (!npc || npc == player || PlayerNpcs.count(npc)) {
                continue;
            }
            float d = (npc->GetPositionWorld() - me).LengthApprox();
            if (d < ProbeNearRadius) {
                closeNpcs.push_back({ d, npc });
            }
        }
        std::sort(closeNpcs.begin(), closeNpcs.end(), [](auto& a, auto& b) { return a.first < b.first; });

        for (size_t i = 0; i < closeNpcs.size() && i < 15; i++) {
            auto npc = closeNpcs[i].second;
            json n;
            n["n"] = std::string(npc->GetInstanceName().ToChar());
            n["p"] = AutoTestPos(npc);
            n["hp"] = npc->GetAttribute(NPC_ATR_HITPOINTS);
            n["ani"] = ProbeActiveAni(npc);
            n["inWorld"] = npc->vobLeafList.GetNum() > 0;
            if (NpcToUniqueNameList.count(npc)) {
                auto unique = NpcToUniqueNameList[npc];
                n["synced"] = SyncNpcs.count(unique) > 0;      // client: driven by the host
                n["broadcast"] = BroadcastNpcs.count(unique) > 0; // host: sent to clients
            }
            list.push_back(n);
        }
        return list;
    }

    static json StoryNearNpcs() {
        return StoryNearAround(player->GetPositionWorld());
    }

    // Host only: NPCs in the host's world around where each remote player last said it was.
    static json StoryNearFriends() {
        json out = json::object();
        if (!ServerThread) {
            return out;
        }
        for (auto& pair : SyncNpcs) {
            auto remote = pair.second;
            if (remote && IsCoopPlayer(remote->name) && remote->lastPositionFromServer) {
                auto p = *remote->lastPositionFromServer;
                json entry;
                entry["at"] = json::array({ (int)p[0], (int)p[1], (int)p[2] });
                entry["npcs"] = StoryNearAround(p);
                out[std::string(remote->name.ToChar())] = entry;
            }
        }
        return out;
    }

    static void ProbeDumpAllNpcs() {
        json all = json::array();
        for (auto node = ogame->GetGameWorld()->voblist_npcs->next; node; node = node->next) {
            auto npc = node->data;
            if (!npc || npc == player) {
                continue;
            }
            all.push_back({ std::string(npc->GetInstanceName().ToChar()), AutoTestPos(npc), npc->GetAttribute(NPC_ATR_HITPOINTS) });
        }
        // waypoints exist whether or not NPCs are spawned around them: good teleport targets
        json wps = json::array();
        auto wayNet = ogame->GetWorld()->wayNet;
        for (auto node = wayNet ? wayNet->wplist.next : NULL; node; node = node->next) {
            if (node->data) {
                auto& p = node->data->pos;
                wps.push_back({ std::string(node->data->name.ToChar()), json::array({ (int)p[0], (int)p[1], (int)p[2] }) });
            }
        }
        json out;
        out["npcs"] = all;
        out["waypoints"] = wps;
        std::ofstream f(std::string(GothicExeFolderPath) + "\\GothicCoopNpcs.json", std::ios_base::trunc | std::ios_base::out);
        f << out.dump();
        json j; j["event"] = "npcs_dumped"; j["count"] = (int)all.size(); j["waypoints"] = (int)wps.size();
        AutoTestWrite(j);
    }

    static oCNpc* ProbeFindNpc(const std::string& instance) {
        auto want = ProbeUpper(instance);
        for (auto node = ogame->GetGameWorld()->voblist_npcs->next; node; node = node->next) {
            auto npc = node->data;
            if (npc && npc != player && ProbeUpper(npc->GetInstanceName().ToChar()) == want) {
                return npc;
            }
        }
        return NULL;
    }

    static void ProbeTalkLoop(long long elapsed) {
        auto& infoMan = oCInformationManager::GetInformationManager();

        if (!ProbeTalkActive && ProbeNextTalk < ProbeTalks.size() && elapsed >= ProbeTalks[ProbeNextTalk].at) {
            auto& t = ProbeTalks[ProbeNextTalk++];
            auto npc = ProbeFindNpc(t.npc);
            json j;
            j["event"] = "talk_start";
            j["npc"] = t.npc;
            j["found"] = npc != NULL;
            if (npc && ProbePauseDuringTalk && ClientThread && !IsCoopPaused) {
                // the same as the F2 key while connected: pause world sync
                IsCoopPaused = true;
                rtnMan->RestartRoutines();
                ProbePausedForTalk = true;
                j["paused"] = true;
            }
            if (npc) {
                // stand next to the NPC, like a player walking up to it
                auto p = npc->GetPositionWorld();
                player->SetCollDet(FALSE);
                // the client walks up from the other side, so the two heroes do not stand in each other
                float side = ClientThread ? -150.f : 120.f;
                player->SetPositionWorld(zVEC3(p[0] + side, p[1] + 20, p[2]));
                player->SetCollDet(TRUE);
                j["npcPos"] = AutoTestPos(npc);
                j["assess"] = npc->AssessTalk_S(player);
                // what the script's AI_ProcessInfos(self) does in ZS_Talk: the NPC opens its infos
                auto msg = new oCMsgConversation(oCMsgConversation::EV_PROCESSINFOS, (zCVob*)player);
                npc->GetEM()->OnMessage(msg, player);
                ProbeTalkActive = true;
                ProbeTalkStartMs = elapsed;
                ProbeTalkLastChoiceMs = elapsed;
                ProbeTalkChoicesLeft = t.choices;
                ProbeTalkNpc = t.npc;
                ProbeTalkNpcPtr = npc;
            }
            AutoTestWrite(j);
            return;
        }

        if (!ProbeTalkActive) {
            return;
        }

        bool open = !infoMan.HasFinished();
        if (open && infoMan.IsWaitingForSelection && elapsed - ProbeTalkLastChoiceMs >= 1500) {
            ProbeTalkLastChoiceMs = elapsed;
            json j;
            j["event"] = "talk_choice";
            j["npc"] = ProbeTalkNpc;
            j["indexBye"] = infoMan.IndexBye;
            // the infos the player can pick now, in menu order (EXIT, nr 999, comes last)
            auto im = ogame->GetInfoManager();
            // argument order is not documented: use whichever order yields infos
            int countNP = im->GetInfoCountUnimportant(ProbeTalkNpcPtr, player);
            int countPN = im->GetInfoCountUnimportant(player, ProbeTalkNpcPtr);
            j["countNpcPlayer"] = countNP;
            j["countPlayerNpc"] = countPN;
            j["countAll"] = im->GetInfoCount(player, ProbeTalkNpcPtr);
            bool playerFirst = countPN > countNP;
            int count = playerFirst ? countPN : countNP;
            json offered = json::array();
            oCInfo* pick = NULL;
            oCInfo* exitInfo = NULL;
            for (int i = 0; i < count; i++) {
                auto info = playerFirst ? im->GetInfoUnimportant(player, ProbeTalkNpcPtr, i) : im->GetInfoUnimportant(ProbeTalkNpcPtr, player, i);
                if (!info) {
                    continue;
                }
                std::string name = info->name.ToChar();
                offered.push_back(name);
                bool isExit = name.find("EXIT") != std::string::npos || name.find("ENDE") != std::string::npos;
                if (isExit && !exitInfo) {
                    exitInfo = info;
                }
                if (!isExit && !pick) {
                    pick = info;
                }
            }
            j["offered"] = offered;
            if (ProbeTalkChoicesLeft-- > 0 && pick) {
                j["pick"] = std::string(pick->name.ToChar());
                infoMan.OnInfo(pick);
            }
            else if (exitInfo) {
                j["pick"] = std::string(exitInfo->name.ToChar());
                infoMan.OnInfo(exitInfo);
            }
            else {
                j["pick"] = "exit";
                infoMan.Exit();
            }
            AutoTestWrite(j);
        }

        // finished: the dialog was opened and is closed again, or nothing opened within 20 s
        bool everOpened = elapsed - ProbeTalkStartMs > 2000;
        if ((everOpened && !open && elapsed - ProbeTalkLastChoiceMs > 2000) || elapsed - ProbeTalkStartMs > 60000) {
            json j;
            j["event"] = "talk_end";
            j["npc"] = ProbeTalkNpc;
            j["ms"] = elapsed - ProbeTalkStartMs;
            j["timedOut"] = elapsed - ProbeTalkStartMs > 60000;
            if (open) {
                infoMan.Exit();
            }
            if (ProbePausedForTalk) {
                IsCoopPaused = false;
                rtnMan->RestartRoutines();
                ProbePausedForTalk = false;
                j["resumed"] = true;
            }
            AutoTestWrite(j);
            ProbeTalkActive = false;
        }
    }

    void StoryProbeLoop(long long elapsed, bool sessionStarted) {
        if (ProbeDumpNpcs && !ProbeNpcsDumped && elapsed > 3000) {
            ProbeNpcsDumped = true;
            ProbeDumpAllNpcs();
        }

        if (ProbeStoryEveryMs > 0 && (ProbeLastStoryMs < 0 || elapsed - ProbeLastStoryMs >= ProbeStoryEveryMs)) {
            ProbeLastStoryMs = elapsed;
            ProbeStorySnapshot();
        }

        if (sessionStarted) {
            ProbeTalkLoop(elapsed);
        }
    }
}
