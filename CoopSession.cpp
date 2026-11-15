namespace GOTHIC_ENGINE {
    // Starting a session was inline in Game_Loop's key handlers; it lives here so the
    // keys and the autotest bot run exactly the same code.
    // The Thread objects are static: the globals keep their address after this returns.
    static Thread CoopServerThreadHandle;
    static Thread CoopClientThreadHandle;

    void StartCoopServer() {
        // "upnp": false skips the router port mapping (not needed over a VPN such as Radmin).
        if (CoopConfig.value("upnp", true)) {
            wchar_t mappedPort[1234];
            std::wcsncpy(mappedPort, L"UDP", 1234);
            new MappedPort(ConnectionPort, mappedPort, mappedPort);
        }

        CoopServerThreadHandle.Init(&CoopServerThread);
        CoopServerThreadHandle.Detach();
        ServerThread = &CoopServerThreadHandle;
        MyselfId = "HOST";
        player->SetAdditionalVisuals(zSTRING("hum_body_Naked0"), MyBodyTextVarNr, DefaultBodyTexColorNr, zSTRING("HUM_HEAD_PONY"), MyHeadVarNr, 0, -1);
    }

    void StartCoopClient() {
        addSyncedNpc("HOST");

        CoopClientThreadHandle.Init(&CoopClientThread);
        CoopClientThreadHandle.Detach();
        ClientThread = &CoopClientThreadHandle;

        ogame->SetTime(ogame->GetWorldTimer()->GetDay(), 12, 00);
        rtnMan->RestartRoutines();
        player->SetAdditionalVisuals(zSTRING("hum_body_Naked0"), MyBodyTextVarNr, DefaultBodyTexColorNr, zSTRING("HUM_HEAD_PONY"), MyHeadVarNr, 0, -1);
    }
}
