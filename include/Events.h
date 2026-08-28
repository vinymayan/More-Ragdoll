#pragma once

#include "PayloadAPI.h"

namespace MoreRagdoll::Events {
    class AnimationEventSink final :
        public RE::BSTEventSink<RE::BSAnimationGraphEvent>,
        public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
    public:
        static AnimationEventSink* GetSingleton();

        void Install();
        void ReconnectHighActors();
        void RegisterActor(RE::Actor* actor, std::uint32_t attempt = 0);

        RE::BSEventNotifyControl ProcessEvent(
            const RE::BSAnimationGraphEvent* event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override;
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESObjectLoadedEvent* event,
            RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override;

    private:
        bool installed_ = false;
    };

    class EnablePayloadHandler final : public payloadinterpreter::PayloadHandler {
    public:
        static EnablePayloadHandler* GetSingleton();
        void Process(
            RE::TESObjectREFR* holder,
            const std::string_view& payload,
            RE::BShkbAnimationGraph*) override;
    };

    class DisablePayloadHandler final : public payloadinterpreter::PayloadHandler {
    public:
        static DisablePayloadHandler* GetSingleton();
        void Process(
            RE::TESObjectREFR* holder,
            const std::string_view& payload,
            RE::BShkbAnimationGraph*) override;
    };

    class AdoptPayloadHandler final : public payloadinterpreter::PayloadHandler {
    public:
        static AdoptPayloadHandler* GetSingleton();
        void Process(
            RE::TESObjectREFR* holder,
            const std::string_view& payload,
            RE::BShkbAnimationGraph*) override;
    };

    class HoldPayloadHandler final : public payloadinterpreter::PayloadHandler {
    public:
        static HoldPayloadHandler* GetSingleton();
        void Process(
            RE::TESObjectREFR* holder,
            const std::string_view& payload,
            RE::BShkbAnimationGraph*) override;
    };

    void RegisterPayloadHandlers(payloadinterpreter::API::PayloadHandlerCollector* collector);
}
