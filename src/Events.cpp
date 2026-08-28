#include "Events.h"

#include "DelayedDispatcher.h"
#include "Manager.h"

MoreRagdoll::Events::AnimationEventSink* MoreRagdoll::Events::AnimationEventSink::GetSingleton() {
    static AnimationEventSink singleton;
    return &singleton;
}

void MoreRagdoll::Events::AnimationEventSink::Install() {
    if (!installed_) {
        if (auto source = RE::ScriptEventSourceHolder::GetSingleton()) {
            source->AddEventSink<RE::TESObjectLoadedEvent>(this);
            installed_ = true;
            logger::info("More Ragdoll actor-load sink installed.");
        } else {
            logger::error("Could not install the More Ragdoll actor-load sink.");
        }
    }
    ReconnectHighActors();
}

void MoreRagdoll::Events::AnimationEventSink::ReconnectHighActors() {
    if (auto player = RE::PlayerCharacter::GetSingleton()) {
        RegisterActor(player);
    }
    if (auto processLists = RE::ProcessLists::GetSingleton()) {
        for (const auto& handle : processLists->highActorHandles) {
            if (const auto actor = handle.get()) {
                RegisterActor(actor.get());
            }
        }
    }
}

void MoreRagdoll::Events::AnimationEventSink::RegisterActor(RE::Actor* actor, std::uint32_t attempt) {
    if (!actor) {
        return;
    }

    RE::BSTSmartPointer<RE::BSAnimationGraphManager> graphManager;
    actor->GetAnimationGraphManager(graphManager);
    if (!graphManager || graphManager->graphs.empty()) {
        if (attempt < 20) {
            const auto handle = actor->GetHandle();
            DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(100), [handle, attempt] {
                SKSE::GetTaskInterface()->AddTask([handle, attempt] {
                    if (const auto actor = handle.get()) {
                        AnimationEventSink::GetSingleton()->RegisterActor(actor.get(), attempt + 1);
                    }
                });
            });
        }
        return;
    }

    for (auto& graph : graphManager->graphs) {
        if (!graph) {
            continue;
        }
        auto* eventSource = static_cast<RE::BSTEventSource<RE::BSAnimationGraphEvent>*>(graph.get());
        eventSource->RemoveEventSink(this);
        eventSource->AddEventSink(this);
    }
}

RE::BSEventNotifyControl MoreRagdoll::Events::AnimationEventSink::ProcessEvent(
    const RE::BSAnimationGraphEvent* event,
    RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!event || !event->holder) {
        return RE::BSEventNotifyControl::kContinue;
    }
    const auto actor = event->holder->As<RE::Actor>();
    if (actor) {
        Manager::HandleAnimationCommand(actor, std::string_view(event->tag));
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl MoreRagdoll::Events::AnimationEventSink::ProcessEvent(
    const RE::TESObjectLoadedEvent* event,
    RE::BSTEventSource<RE::TESObjectLoadedEvent>*) {
    if (!event) {
        return RE::BSEventNotifyControl::kContinue;
    }
    if (!event->loaded) {
        Manager::Forget(event->formID);
        return RE::BSEventNotifyControl::kContinue;
    }

    if (const auto actor = RE::TESForm::LookupByID<RE::Actor>(event->formID)) {
        RegisterActor(actor);
    }
    return RE::BSEventNotifyControl::kContinue;
}

MoreRagdoll::Events::EnablePayloadHandler* MoreRagdoll::Events::EnablePayloadHandler::GetSingleton() {
    static EnablePayloadHandler singleton;
    return &singleton;
}

void MoreRagdoll::Events::EnablePayloadHandler::Process(
    RE::TESObjectREFR* holder,
    const std::string_view& payload,
    RE::BShkbAnimationGraph*) {
    const auto actor = holder ? holder->As<RE::Actor>() : nullptr;
    if (!actor) {
        return;
    }
    if (payload.empty()) {
        Manager::RequestEnable(actor);
    } else {
        Manager::HandleAnimationCommand(actor, std::string("MoreRagdollStart.") + std::string(payload));
    }
}

MoreRagdoll::Events::DisablePayloadHandler* MoreRagdoll::Events::DisablePayloadHandler::GetSingleton() {
    static DisablePayloadHandler singleton;
    return &singleton;
}

MoreRagdoll::Events::AdoptPayloadHandler* MoreRagdoll::Events::AdoptPayloadHandler::GetSingleton() {
    static AdoptPayloadHandler singleton;
    return &singleton;
}

void MoreRagdoll::Events::AdoptPayloadHandler::Process(
    RE::TESObjectREFR* holder,
    const std::string_view& payload,
    RE::BShkbAnimationGraph*) {
    const auto actor = holder ? holder->As<RE::Actor>() : nullptr;
    if (!actor) {
        return;
    }
    if (payload.empty()) {
        Manager::RequestAdopt(actor);
        return;
    }

    const std::string command = std::string("MoreRagdollAdopt.") + std::string(payload);
    Manager::HandleAnimationCommand(actor, command);
}

void MoreRagdoll::Events::DisablePayloadHandler::Process(
    RE::TESObjectREFR* holder,
    const std::string_view&,
    RE::BShkbAnimationGraph*) {
    if (const auto actor = holder ? holder->As<RE::Actor>() : nullptr) {
        Manager::RequestDisable(actor);
    }
}

MoreRagdoll::Events::HoldPayloadHandler* MoreRagdoll::Events::HoldPayloadHandler::GetSingleton() {
    static HoldPayloadHandler singleton;
    return &singleton;
}

void MoreRagdoll::Events::HoldPayloadHandler::Process(
    RE::TESObjectREFR* holder,
    const std::string_view& payload,
    RE::BShkbAnimationGraph*) {
    if (const auto actor = holder ? holder->As<RE::Actor>() : nullptr) {
        Manager::HandleAnimationCommand(actor, std::string("MoreRagdollHold.") + std::string(payload));
    }
}

void MoreRagdoll::Events::RegisterPayloadHandlers(
    payloadinterpreter::API::PayloadHandlerCollector* collector) {
    if (!collector) {
        return;
    }
    collector->RegisterPayloadHandler("MoreRagdollEnable", EnablePayloadHandler::GetSingleton());
    collector->RegisterPayloadHandler("MoreRagdollStart", HoldPayloadHandler::GetSingleton());
    collector->RegisterPayloadHandler("MoreRagdollAdopt", AdoptPayloadHandler::GetSingleton());
    collector->RegisterPayloadHandler("MoreRagdollDisable", DisablePayloadHandler::GetSingleton());
    collector->RegisterPayloadHandler("MoreRagdollHold", HoldPayloadHandler::GetSingleton());
    logger::info("Payload Interpreter handlers registered for More Ragdoll.");
}
