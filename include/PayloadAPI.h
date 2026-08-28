#pragma once

namespace RE {
    class BShkbAnimationGraph;
    class TESObjectREFR;
}

namespace payloadinterpreter {
    class PayloadHandler {
    public:
        virtual ~PayloadHandler() = default;
        virtual void Process(
            RE::TESObjectREFR* holder,
            const std::string_view& payload,
            RE::BShkbAnimationGraph* animationGraph) = 0;
    };

    namespace API {
        class PayloadHandlerCollector {
        public:
            virtual void RegisterPayloadHandler(const char* eventTag, PayloadHandler* payloadHandler) = 0;
        };

        struct Message {
            PayloadHandlerCollector* payloadHandlerCollector;
        };
    }
}
