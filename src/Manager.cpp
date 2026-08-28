#include "Manager.h"

#include "DelayedDispatcher.h"

#include <limits>

namespace {
    struct HoldEntry {
        RE::ActorHandle handle;
        std::uint64_t generation{ 0 };
    };

    std::mutex holdMutex;
    std::unordered_map<RE::FormID, HoldEntry> heldActors;
    std::atomic_uint64_t nextGeneration{ 0 };

    bool IsAlive(RE::Actor* actor) {
        if (!actor || actor->IsDead()) {
            return false;
        }
        const auto lifeState = actor->GetLifeState();
        return lifeState != RE::ACTOR_LIFE_STATE::kDead &&
               lifeState != RE::ACTOR_LIFE_STATE::kDying;
    }

    bool IsAlreadyRagdolled(RE::Actor* actor) {
        if (!actor) {
            return false;
        }
        if (actor->IsInRagdollState()) {
            return true;
        }

        const auto actorState = actor->AsActorState();
        if (!actorState) {
            return false;
        }

        // IsInRagdollState is authoritative once Havok owns the actor.  The
        // knock state also covers the short graph/physics transition in which
        // a spell, fall or killmove has requested ragdoll but Havok has not yet
        // reported the final state.
        switch (actorState->GetKnockState()) {
        case RE::KNOCK_STATE_ENUM::kExplode:
        case RE::KNOCK_STATE_ENUM::kExplodeLeadIn:
        case RE::KNOCK_STATE_ENUM::kOut:
        case RE::KNOCK_STATE_ENUM::kOutLeadIn:
        case RE::KNOCK_STATE_ENUM::kQueued:
        case RE::KNOCK_STATE_ENUM::kDown:
        case RE::KNOCK_STATE_ENUM::kWaitForTaskQueue:
            return true;
        default:
            return false;
        }
    }

    bool StartNativeRagdoll(RE::Actor* actor) {
        if (!actor || !actor->Is3DLoaded()) {
            logger::warn("Could not start native ragdoll: actor or loaded 3D is unavailable.");
            return false;
        }

        const auto controller = actor->GetCharController();
        if (!controller || !controller->GetHavokWorld()) {
            logger::warn(
                "Could not start native ragdoll for {:08X}: character controller is not in the Havok world.",
                actor->GetFormID());
            return false;
        }

        auto* process = actor->GetActorRuntimeData().currentProcess;
        if (!process || !process->InHighProcess()) {
            logger::warn(
                "Could not start native ragdoll for {:08X}: actor has no high AI process.",
                actor->GetFormID());
            return false;
        }

        const auto location = actor->GetPosition();
        constexpr float magnitude = std::numeric_limits<float>::min();
        process->KnockExplosion(actor, location, magnitude);
        return true;
    }

    bool SetHoldVariable(RE::Actor* actor, bool enabled, bool reportFailure = true) {
        if (!actor) {
            return false;
        }

        const RE::BSFixedString variableName{ MoreRagdoll::Manager::GRAPH_HOLD_VARIABLE };
        const bool setSucceeded = actor->SetGraphVariableBool(variableName, enabled);
        bool actualValue = !enabled;
        const bool readSucceeded = actor->GetGraphVariableBool(variableName, actualValue);
        const bool verified = setSucceeded && readSucceeded && actualValue == enabled;
        if (!verified && reportFailure) {
            logger::warn(
                "Could not verify behavior variable '{}' for {:08X} (set={}, read={}, actual={}, expected={}). "
                "Regenerate behaviors with the 'moreragdoll' patch enabled.",
                MoreRagdoll::Manager::GRAPH_HOLD_VARIABLE,
                actor->GetFormID(),
                setSucceeded,
                readSucceeded,
                actualValue,
                enabled);
        }
        return verified;
    }

    std::string_view Trim(std::string_view value) {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.remove_prefix(1);
        }
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
            value.remove_suffix(1);
        }
        return value;
    }

    bool ParseBool(std::string_view value, bool& parsed) {
        value = Trim(value);
        if (value == "true" || value == "TRUE" || value == "True" || value == "1") {
            parsed = true;
            return true;
        }
        if (value == "false" || value == "FALSE" || value == "False" || value == "0") {
            parsed = false;
            return true;
        }
        return false;
    }

    bool ParseTimedOptions(
        std::string_view payload,
        float& seconds,
        bool& forceGetUpOnTimeout) {
        while (!payload.empty() && (payload.front() == '.' || payload.front() == '|')) {
            payload.remove_prefix(1);
        }
        payload = Trim(payload);
        if (payload.empty()) {
            return false;
        }

        const auto separator = payload.find('|');
        const auto secondsValue = Trim(payload.substr(0, separator));
        if (secondsValue.empty()) {
            return false;
        }

        float parsed = 0.0F;
        const auto result = std::from_chars(
            secondsValue.data(),
            secondsValue.data() + secondsValue.size(),
            parsed);
        if (result.ec != std::errc{} || result.ptr != secondsValue.data() + secondsValue.size() ||
            !std::isfinite(parsed) || parsed <= 0.0F) {
            return false;
        }

        forceGetUpOnTimeout = true;
        if (separator != std::string_view::npos) {
            const auto boolValue = payload.substr(separator + 1);
            if (boolValue.empty() || boolValue.find('|') != std::string_view::npos ||
                !ParseBool(boolValue, forceGetUpOnTimeout)) {
                return false;
            }
        }

        seconds = std::clamp(parsed, 0.05F, 3600.0F);
        return true;
    }

    void DisableNow(RE::Actor* actor, bool forceGetUp) {
        if (!actor) {
            return;
        }

        bool wasHeld = false;
        {
            std::scoped_lock lock(holdMutex);
            wasHeld = heldActors.erase(actor->GetFormID()) > 0;
        }
        if (!wasHeld) {
            return;
        }

        SetHoldVariable(actor, false, false);
        if (IsAlive(actor) && forceGetUp) {
            // Fallout from a fall/killmove can leave the behavior graph alive
            // while the character controller is still detached. Let the
            // engine reconcile that native state before starting get-up.
            actor->PotentiallyFixRagdollState();
            actor->NotifyAnimationGraph("GetUpBegin");
        }
    }

    void ScheduleTimeout(
        RE::FormID formID,
        std::uint64_t generation,
        float timeoutSeconds,
        bool forceGetUpOnTimeout) {
        const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<float>(timeoutSeconds));
        MoreRagdoll::DelayedDispatcher::Get().PostDelayed(
            delay,
            [formID, generation, forceGetUpOnTimeout] {
                SKSE::GetTaskInterface()->AddTask([formID, generation, forceGetUpOnTimeout] {
                    RE::ActorHandle handle;
                    {
                        std::scoped_lock lock(holdMutex);
                        const auto it = heldActors.find(formID);
                        if (it == heldActors.end() || it->second.generation != generation) {
                            return;
                        }
                        handle = it->second.handle;
                    }

                    const auto actor = handle.get();
                    if (!actor) {
                        std::scoped_lock lock(holdMutex);
                        const auto it = heldActors.find(formID);
                        if (it != heldActors.end() && it->second.generation == generation) {
                            heldActors.erase(it);
                        }
                        return;
                    }

                    if (!IsAlive(actor.get())) {
                        DisableNow(actor.get(), false);
                        return;
                    }
                    DisableNow(actor.get(), forceGetUpOnTimeout);
                });
            });
    }

    void EnableNow(
        RE::Actor* actor,
        float timeoutSeconds,
        bool adoptOnly,
        bool forceGetUpOnTimeout) {
        if (!IsAlive(actor)) {
            return;
        }

        const bool alreadyRagdolled = IsAlreadyRagdolled(actor);
        timeoutSeconds = std::clamp(timeoutSeconds, 0.05F, 3600.0F);
        const auto generation = nextGeneration.fetch_add(1) + 1;
        {
            std::scoped_lock lock(holdMutex);
            heldActors.insert_or_assign(
                actor->GetFormID(),
                HoldEntry{ actor->GetHandle(), generation });
        }

        // Arm the behavior gate before doing anything that can cause a get-up
        // transition. This lets More Ragdoll adopt ragdolls created by spells,
        // falls and killmoves without restarting their Havok state.
        SetHoldVariable(actor, true);
        if (!adoptOnly && !alreadyRagdolled) {
            StartNativeRagdoll(actor);
        }

        const auto handle = actor->GetHandle();
        MoreRagdoll::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(50), [handle, generation] {
            SKSE::GetTaskInterface()->AddTask([handle, generation] {
                const auto actor = handle.get();
                if (!actor) {
                    return;
                }
                {
                    std::scoped_lock lock(holdMutex);
                    const auto it = heldActors.find(actor->GetFormID());
                    if (it == heldActors.end() || it->second.generation != generation) {
                        return;
                    }
                }
                SetHoldVariable(actor.get(), true);
            });
        });
        ScheduleTimeout(actor->GetFormID(), generation, timeoutSeconds, forceGetUpOnTimeout);
    }
}

bool MoreRagdoll::Manager::RequestEnable(RE::Actor* actor, float timeoutSeconds) {
    if (!actor || !std::isfinite(timeoutSeconds) || timeoutSeconds <= 0.0F) {
        return false;
    }
    const auto handle = actor->GetHandle();
    SKSE::GetTaskInterface()->AddTask([handle, timeoutSeconds] {
        if (const auto actor = handle.get()) {
            EnableNow(actor.get(), timeoutSeconds, false, true);
        }
    });
    return true;
}

bool MoreRagdoll::Manager::RequestEnable(RE::FormID formID, float timeoutSeconds) {
    if (formID == 0 || !std::isfinite(timeoutSeconds) || timeoutSeconds <= 0.0F) {
        return false;
    }
    SKSE::GetTaskInterface()->AddTask([formID, timeoutSeconds] {
        if (const auto actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
            EnableNow(actor, timeoutSeconds, false, true);
        } else {
            logger::warn("Could not enable More Ragdoll: {:08X} is not a loaded actor.", formID);
        }
    });
    return true;
}

bool MoreRagdoll::Manager::RequestAdopt(
    RE::Actor* actor,
    float timeoutSeconds,
    bool forceGetUpOnTimeout) {
    if (!actor || !std::isfinite(timeoutSeconds) || timeoutSeconds <= 0.0F) {
        return false;
    }
    const auto handle = actor->GetHandle();
    SKSE::GetTaskInterface()->AddTask([handle, timeoutSeconds, forceGetUpOnTimeout] {
        if (const auto actor = handle.get()) {
            EnableNow(actor.get(), timeoutSeconds, true, forceGetUpOnTimeout);
        }
    });
    return true;
}

bool MoreRagdoll::Manager::RequestAdopt(
    RE::FormID formID,
    float timeoutSeconds,
    bool forceGetUpOnTimeout) {
    if (formID == 0 || !std::isfinite(timeoutSeconds) || timeoutSeconds <= 0.0F) {
        return false;
    }
    SKSE::GetTaskInterface()->AddTask([formID, timeoutSeconds, forceGetUpOnTimeout] {
        if (const auto actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
            EnableNow(actor, timeoutSeconds, true, forceGetUpOnTimeout);
        } else {
            logger::warn("Could not adopt More Ragdoll: {:08X} is not a loaded actor.", formID);
        }
    });
    return true;
}

bool MoreRagdoll::Manager::RequestDisable(RE::Actor* actor) {
    if (!actor) {
        return false;
    }
    const auto handle = actor->GetHandle();
    SKSE::GetTaskInterface()->AddTask([handle] {
        if (const auto actor = handle.get()) {
            DisableNow(actor.get(), true);
        }
    });
    return true;
}

bool MoreRagdoll::Manager::RequestDisable(RE::FormID formID) {
    if (formID == 0) {
        return false;
    }
    SKSE::GetTaskInterface()->AddTask([formID] {
        if (const auto actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
            DisableNow(actor, true);
        } else {
            Forget(formID);
            logger::warn("Could not disable More Ragdoll cleanly: {:08X} is not a loaded actor.", formID);
        }
    });
    return true;
}

bool MoreRagdoll::Manager::RequestHold(RE::Actor* actor, float seconds) {
    return RequestEnable(actor, seconds);
}

bool MoreRagdoll::Manager::RequestHold(RE::FormID formID, float seconds) {
    return RequestEnable(formID, seconds);
}

bool MoreRagdoll::Manager::RequestStartRagdoll(
    RE::Actor* actor,
    float seconds,
    bool forceGetUpOnTimeout) {
    if (!actor || !std::isfinite(seconds) || seconds <= 0.0F) {
        return false;
    }
    const auto handle = actor->GetHandle();
    SKSE::GetTaskInterface()->AddTask([handle, seconds, forceGetUpOnTimeout] {
        if (const auto actor = handle.get()) {
            EnableNow(actor.get(), seconds, false, forceGetUpOnTimeout);
        }
    });
    return true;
}

bool MoreRagdoll::Manager::RequestStartRagdoll(
    RE::FormID formID,
    float seconds,
    bool forceGetUpOnTimeout) {
    if (formID == 0 || !std::isfinite(seconds) || seconds <= 0.0F) {
        return false;
    }
    SKSE::GetTaskInterface()->AddTask([formID, seconds, forceGetUpOnTimeout] {
        if (const auto actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
            EnableNow(actor, seconds, false, forceGetUpOnTimeout);
        } else {
            logger::warn("Could not start More Ragdoll: {:08X} is not a loaded actor", formID);
        }
    });
    return true;
}

bool MoreRagdoll::Manager::HandleAnimationCommand(RE::Actor* actor, std::string_view eventName) {
    if (!actor) {
        return false;
    }
    if (eventName == "MoreRagdollEnable") {
        RequestEnable(actor);
        return true;
    }
    if (eventName == "MoreRagdollStart") {
        RequestStartRagdoll(actor, DEFAULT_SAFETY_TIMEOUT_SECONDS, true);
        return true;
    }
    if (eventName == "MoreRagdollAdopt") {
        RequestAdopt(actor);
        return true;
    }
    if (eventName == "MoreRagdollDisable") {
        RequestDisable(actor);
        return true;
    }

    constexpr std::string_view startPrefix = "MoreRagdollStart";
    constexpr std::string_view holdPrefix = "MoreRagdollHold";
    if (eventName.starts_with(startPrefix) || eventName.starts_with(holdPrefix)) {
        const auto prefix = eventName.starts_with(startPrefix) ? startPrefix : holdPrefix;
        float seconds = 0.0F;
        bool forceGetUpOnTimeout = true;
        if (!ParseTimedOptions(
                eventName.substr(prefix.size()),
                seconds,
                forceGetUpOnTimeout)) {
            logger::warn("Rejected invalid timed More Ragdoll event '{}'.", eventName);
            return true;
        }
        RequestStartRagdoll(actor, seconds, forceGetUpOnTimeout);
        return true;
    }

    constexpr std::string_view adoptPrefix = "MoreRagdollAdopt";
    if (eventName.starts_with(adoptPrefix)) {
        float seconds = 0.0F;
        bool forceGetUpOnTimeout = true;
        if (!ParseTimedOptions(
                eventName.substr(adoptPrefix.size()),
                seconds,
                forceGetUpOnTimeout)) {
            logger::warn("Rejected invalid MoreRagdollAdopt event '{}'.", eventName);
            return true;
        }
        RequestAdopt(actor, seconds, forceGetUpOnTimeout);
        return true;
    }
    return false;
}

bool MoreRagdoll::Manager::ShouldBlockGetUp(RE::Actor* actor, std::string_view eventName) {
    return actor && (eventName == "GetUpBegin" || eventName == "GetUpStart") && IsHeld(actor);
}

bool MoreRagdoll::Manager::IsHeld(RE::Actor* actor) {
    return actor && IsHeld(actor->GetFormID());
}

bool MoreRagdoll::Manager::IsHeld(RE::FormID formID) {
    if (formID == 0) {
        return false;
    }
    std::scoped_lock lock(holdMutex);
    return heldActors.contains(formID);
}

void MoreRagdoll::Manager::Forget(RE::FormID formID) {
    std::scoped_lock lock(holdMutex);
    heldActors.erase(formID);
}

void MoreRagdoll::Manager::Reset() {
    std::scoped_lock lock(holdMutex);
    heldActors.clear();
    nextGeneration.fetch_add(1);
}
