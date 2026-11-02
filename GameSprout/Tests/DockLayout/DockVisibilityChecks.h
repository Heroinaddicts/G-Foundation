#pragma once

// Exercise the real public Docker contract, including the Qt tabified case in
// which the inactive dock and its child still report QWidget::isVisible().
static void CheckDockContentVisibility(IIDEWindow* ide, IDockerLayoutManager* layouts,
                                       QMainWindow* host) {
    auto* scene = ide->QueryDocker("Scene");
    auto* game = ide->QueryDocker("Game");
    auto* sceneWidget = host->findChild<QDockWidget*>("Scene");
    auto* gameWidget = host->findChild<QDockWidget*>("Game");
    Check(scene && game && sceneWidget && gameWidget, "Visibility docks missing");
    Check(scene->IsContentVisible() && !game->IsContentVisible(),
          "Default Scene tab must be the only visible content in its group");
    Check(gameWidget->isVisible() && gameWidget->widget()->isVisible(),
          "Inactive tab fixture must expose the QWidget visibility ambiguity");

    int sceneOwner = 0, gameOwner = 0;
    bool sceneVisible = scene->IsContentVisible(), gameVisible = game->IsContentVisible();
    int sceneEvents = 0, gameEvents = 0;
    Check(!scene->RegisterContentVisibilityCallback(nullptr, [](bool) {}), "Null visibility owner accepted");
    Check(!scene->RegisterContentVisibilityCallback(&sceneOwner, {}), "Empty visibility callback accepted");
    Check(scene->RegisterContentVisibilityCallback(&sceneOwner, [&](bool value) {
        sceneVisible = value; ++sceneEvents;
    }), "Cannot subscribe Scene visibility");
    Check(game->RegisterContentVisibilityCallback(&gameOwner, [&](bool value) {
        gameVisible = value; ++gameEvents;
    }), "Cannot subscribe Game visibility");
    Check(!scene->RegisterContentVisibilityCallback(&sceneOwner, [](bool) {}), "Duplicate visibility owner accepted");
    Check(sceneEvents == 0 && gameEvents == 0, "Subscription unexpectedly replayed a visibility event");

    game->Activate(); Settle(50);
    Check(!sceneVisible && gameVisible && !scene->IsContentVisible() && game->IsContentVisible(),
          "Selecting Game did not publish both tab visibility transitions");
    scene->Activate(); Settle(50);
    Check(sceneVisible && !gameVisible, "Selecting Scene did not hide Game content");
    gameWidget->setFloating(true); game->Activate(); Settle(80);
    Check(gameWidget->isFloating() && sceneVisible && gameVisible,
          "Floating Game must allow Scene and Game content simultaneously");
    gameWidget->hide(); Settle(50);
    Check(!gameVisible && !game->IsContentVisible(), "Hidden floating content still visible");
    game->Activate(); Settle(50);
    Check(gameVisible, "Reopened floating content stayed hidden");
    gameWidget->close(); Settle(50);
    Check(!gameVisible, "Closing floating content did not publish hidden state");

    scene->UnregisterContentVisibilityCallback(&sceneOwner);
    game->UnregisterContentVisibilityCallback(&gameOwner);
    const int previousSceneEvents = sceneEvents, previousGameEvents = gameEvents;
    Check(layouts->ResetDefaultLayout(), "Cannot reset layout after visibility checks"); Settle(100);
    game->Activate(); Settle(50); scene->Activate(); Settle(50);
    Check(sceneEvents == previousSceneEvents && gameEvents == previousGameEvents,
          "Unregistered content visibility callback was called");
    Check(scene->IsContentVisible() && !game->IsContentVisible(),
          "Content visibility snapshot stopped updating after unsubscribe");
    std::cout << "PASS Docker content visibility: tab selection, floating, hide, close and unsubscribe\n";
}
