#include "Papyrus.h"

#include "Manager.h"
#include "MoreRagdollAPI.h"

namespace {
    constexpr std::string_view SCRIPT_NAME = "MoreRagdoll";

    std::int32_t PapyrusGetVersion(RE::StaticFunctionTag*) {
        return static_cast<std::int32_t>(MoreRagdollAPI::API_VERSION);
    }

    bool PapyrusStartRagdoll(
        RE::StaticFunctionTag*,
        RE::Actor* actor,
        float durationSeconds,
        bool forceGetUpOnTimeout) {
        return MoreRagdoll::Manager::RequestStartRagdoll(
            actor,
            durationSeconds,
            forceGetUpOnTimeout);
    }

    bool PapyrusAdoptRagdoll(
        RE::StaticFunctionTag*,
        RE::Actor* actor,
        float durationSeconds,
        bool forceGetUpOnTimeout) {
        return MoreRagdoll::Manager::RequestAdopt(
            actor,
            durationSeconds,
            forceGetUpOnTimeout);
    }

    bool PapyrusDisableRagdoll(RE::StaticFunctionTag*, RE::Actor* actor) {
        return MoreRagdoll::Manager::RequestDisable(actor);
    }

    bool PapyrusIsHeld(RE::StaticFunctionTag*, RE::Actor* actor) {
        return MoreRagdoll::Manager::IsHeld(actor);
    }
}

bool MoreRagdoll::Papyrus::Register(RE::BSScript::IVirtualMachine* vm) {
    if (!vm) {
        return false;
    }

    vm->RegisterFunction("GetVersion", SCRIPT_NAME, PapyrusGetVersion, true);
    vm->RegisterFunction("StartRagdoll", SCRIPT_NAME, PapyrusStartRagdoll);
    vm->RegisterFunction("AdoptRagdoll", SCRIPT_NAME, PapyrusAdoptRagdoll);
    vm->RegisterFunction("DisableRagdoll", SCRIPT_NAME, PapyrusDisableRagdoll);
    vm->RegisterFunction("IsHeld", SCRIPT_NAME, PapyrusIsHeld, true);
    logger::info("More Ragdoll Papyrus API version {} registered.", MoreRagdollAPI::API_VERSION);
    return true;
}
