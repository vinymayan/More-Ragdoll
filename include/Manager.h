#pragma once

namespace MoreRagdoll::Manager {
    inline constexpr float DEFAULT_SAFETY_TIMEOUT_SECONDS = 120.0F;
    inline constexpr std::string_view GRAPH_HOLD_VARIABLE = "MoreRagdollActive";

    bool RequestEnable(RE::Actor* actor, float timeoutSeconds = DEFAULT_SAFETY_TIMEOUT_SECONDS);
    bool RequestEnable(RE::FormID formID, float timeoutSeconds = DEFAULT_SAFETY_TIMEOUT_SECONDS);
    bool RequestAdopt(
        RE::Actor* actor,
        float timeoutSeconds = DEFAULT_SAFETY_TIMEOUT_SECONDS,
        bool forceGetUpOnTimeout = true);
    bool RequestAdopt(
        RE::FormID formID,
        float timeoutSeconds = DEFAULT_SAFETY_TIMEOUT_SECONDS,
        bool forceGetUpOnTimeout = true);
    bool RequestDisable(RE::Actor* actor);
    bool RequestDisable(RE::FormID formID);
    bool RequestHold(RE::Actor* actor, float seconds);
    bool RequestHold(RE::FormID formID, float seconds);
    bool RequestStartRagdoll(RE::Actor* actor, float seconds, bool forceGetUpOnTimeout);
    bool RequestStartRagdoll(RE::FormID formID, float seconds, bool forceGetUpOnTimeout);

    bool HandleAnimationCommand(RE::Actor* actor, std::string_view eventName);
    bool ShouldBlockGetUp(RE::Actor* actor, std::string_view eventName);
    bool IsHeld(RE::Actor* actor);
    bool IsHeld(RE::FormID formID);

    void Forget(RE::FormID formID);
    void Reset();
}
