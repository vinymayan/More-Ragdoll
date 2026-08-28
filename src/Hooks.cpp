#include "Hooks.h"

#include "Manager.h"

namespace {
    struct NotifyAnimationGraphHook {
        using Function_t = bool (*)(RE::IAnimationGraphManagerHolder*, const RE::BSFixedString&);

        enum class Result : std::uint8_t {
            kForward,
            kConsumed,
            kBlocked
        };

        static Result Handle(
            RE::IAnimationGraphManagerHolder* holder,
            const RE::BSFixedString& eventName) {
            if (eventName.empty()) {
                return Result::kForward;
            }
            const auto actor = skyrim_cast<RE::Actor*>(holder);
            if (!actor) {
                return Result::kForward;
            }

            const std::string_view name(eventName.c_str());
            if (MoreRagdoll::Manager::HandleAnimationCommand(actor, name)) {
                return Result::kConsumed;
            }
            if (MoreRagdoll::Manager::ShouldBlockGetUp(actor, name)) {
                return Result::kBlocked;
            }
            return Result::kForward;
        }

        static bool RefrThunk(
            RE::IAnimationGraphManagerHolder* holder,
            const RE::BSFixedString& eventName) {
            const auto result = Handle(holder, eventName);
            if (result == Result::kConsumed) {
                return true;
            }
            if (result == Result::kBlocked) {
                return false;
            }
            return RefrFunction(holder, eventName);
        }

        static bool CharacterThunk(
            RE::IAnimationGraphManagerHolder* holder,
            const RE::BSFixedString& eventName) {
            const auto result = Handle(holder, eventName);
            if (result == Result::kConsumed) {
                return true;
            }
            if (result == Result::kBlocked) {
                return false;
            }
            return CharacterFunction(holder, eventName);
        }

        static bool PlayerThunk(
            RE::IAnimationGraphManagerHolder* holder,
            const RE::BSFixedString& eventName) {
            const auto result = Handle(holder, eventName);
            if (result == Result::kConsumed) {
                return true;
            }
            if (result == Result::kBlocked) {
                return false;
            }
            return PlayerFunction(holder, eventName);
        }

        static void Install() {
            REL::Relocation<std::uintptr_t> refrVtable{ RE::VTABLE_TESObjectREFR[3] };
            RefrFunction = refrVtable.write_vfunc(0x1, RefrThunk);

            REL::Relocation<std::uintptr_t> characterVtable{ RE::VTABLE_Character[3] };
            CharacterFunction = characterVtable.write_vfunc(0x1, CharacterThunk);

            REL::Relocation<std::uintptr_t> playerVtable{ RE::VTABLE_PlayerCharacter[3] };
            PlayerFunction = playerVtable.write_vfunc(0x1, PlayerThunk);
        }

        static inline REL::Relocation<Function_t> RefrFunction;
        static inline REL::Relocation<Function_t> CharacterFunction;
        static inline REL::Relocation<Function_t> PlayerFunction;
    };
}

void MoreRagdoll::Hooks::Install() {
    static std::once_flag installed;
    std::call_once(installed, [] {
        NotifyAnimationGraphHook::Install();
        logger::info("More Ragdoll NotifyAnimationGraph hooks installed for references, characters and player.");
    });
}
