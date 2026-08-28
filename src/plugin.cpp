#include "logger.h"
#include "Events.h"
#include "Hooks.h"
#include "Manager.h"
#include "MoreRagdollAPI.h"
#include "Papyrus.h"

namespace {
    class MoreRagdollInterface final : public MoreRagdollAPI::Interface {
    public:
        std::uint32_t GetVersion() const noexcept override {
            return MoreRagdollAPI::API_VERSION;
        }

        bool Enable(std::uint32_t actorFormID, float safetyTimeoutSeconds) override {
            return MoreRagdoll::Manager::RequestEnable(actorFormID, safetyTimeoutSeconds);
        }

        bool Disable(std::uint32_t actorFormID) override {
            return MoreRagdoll::Manager::RequestDisable(actorFormID);
        }

        bool Hold(std::uint32_t actorFormID, float seconds) override {
            return MoreRagdoll::Manager::RequestHold(actorFormID, seconds);
        }

        bool IsHeld(std::uint32_t actorFormID) const override {
            return MoreRagdoll::Manager::IsHeld(actorFormID);
        }

        bool Adopt(std::uint32_t actorFormID, float safetyTimeoutSeconds) override {
            return MoreRagdoll::Manager::RequestAdopt(actorFormID, safetyTimeoutSeconds);
        }

        bool StartRagdoll(
            std::uint32_t actorFormID,
            float durationSeconds,
            bool forceGetUpOnTimeout) override {
            return MoreRagdoll::Manager::RequestStartRagdoll(
                actorFormID,
                durationSeconds,
                forceGetUpOnTimeout);
        }
    };

    MoreRagdollInterface api;

    void PayloadInterpreterMessageListener(SKSE::MessagingInterface::Message* message) {
        if (!message || std::string_view(message->sender) != "PayloadInterpreter") {
            return;
        }
        const auto payloadMessage = static_cast<payloadinterpreter::API::Message*>(message->data);
        if (payloadMessage) {
            MoreRagdoll::Events::RegisterPayloadHandlers(payloadMessage->payloadHandlerCollector);
        }
    }
}

extern "C" __declspec(dllexport) MoreRagdollAPI::Interface* GetMoreRagdollAPI() {
    return &api;
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (!message) {
        return;
    }
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        if (SKSE::GetMessagingInterface()->RegisterListener(
                "PayloadInterpreter", PayloadInterpreterMessageListener)) {
            logger::info("Payload Interpreter listener registered.");
        } else {
            logger::info("Payload Interpreter is not installed; direct animation events and the C++ API remain available.");
        }
    } else if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        MoreRagdoll::Events::AnimationEventSink::GetSingleton()->Install();
    } else if (message->type == SKSE::MessagingInterface::kNewGame ||
               message->type == SKSE::MessagingInterface::kPostLoadGame) {
        MoreRagdoll::Manager::Reset();
        MoreRagdoll::Events::AnimationEventSink::GetSingleton()->ReconnectHighActors();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded.");
    SKSE::Init(skse);
    const auto papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(MoreRagdoll::Papyrus::Register)) {
        logger::error("Could not register the More Ragdoll Papyrus API.");
    }
    MoreRagdoll::Hooks::Install();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
