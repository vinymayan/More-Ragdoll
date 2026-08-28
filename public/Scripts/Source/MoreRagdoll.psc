Scriptname MoreRagdoll Hidden

; Returns the public API version exposed by the native plugin.
int Function GetVersion() global native

; Starts a new ragdoll, or adopts the current one, and holds it for the given duration.
; If abForceGetUpOnTimeout is true, the plugin sends the get-up flow when time expires.
; If false, it only releases the hold and lets Skyrim get the actor up naturally.
bool Function StartRagdoll(Actor akActor, float afDurationSeconds, bool abForceGetUpOnTimeout = true) global native

; Holds an externally-started ragdoll without requesting a new one.
bool Function AdoptRagdoll(Actor akActor, float afDurationSeconds, bool abForceGetUpOnTimeout = true) global native

; Releases the hold immediately and forces the get-up flow.
bool Function DisableRagdoll(Actor akActor) global native

; Returns whether the actor is currently controlled by More Ragdoll.
bool Function IsHeld(Actor akActor) global native
