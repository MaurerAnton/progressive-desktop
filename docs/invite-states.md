# docs/invite-states.md — Matrix Invite State Reference

> **Source:** Matrix v1.11 Client-Server API spec. Research session July 27, 2026.
> Covers all documented scenarios where an invite becomes "stuck" — user cannot accept or reject.

---

## Standard Flow (working)

```
invite → accept (joinRoom) → join
invite → reject (leaveRoom) → leave
leave  → forgetRoom       → removed from /sync permanently
```

---

## Stuck Invite Scenarios (8 cases)

### 1. Server Notices Room
- **Error:** `M_CANNOT_LEAVE_SERVER_NOTICE_ROOM`
- **Symptom:** Cannot reject invite. Cannot leave after joining.
- **Spec:** "Server Notices" module — client MUST NOT expect to be able to reject this invite.
- **Client action:** Show "Hide from list" local-only option.

### 2. Restricted Room — conditions not met
- **Error:** `403 M_FORBIDDEN` on join
- **Symptom:** Can accept IF directly invited; CANNOT accept if conditions (m.room_membership allow rules) fail.
- **Client action:** Show errcode. Offer "Forget" / "Hide from list".

### 3. Incompatible Room Version
- **Error:** `M_INCOMPATIBLE_ROOM_VERSION` on join (includes `room_version` field)
- **Symptom:** Homeserver doesn't support the room's version.
- **Client action:** Show errcode + room version. "Hide from list".

### 4. Already Joined (stale client state)
- **Symptom:** Client shows `isInvite = true` but server has user as `join`. Accept works (joinRoom is idempotent, returns success). Reject calls leaveRoom — this would actually LEAVE the joined room (dangerous).
- **Root cause:** Stale sync — user joined on another device, our last sync still showed `invite`.
- **Client action:** After joinRoom succeeds, set `isInvite = false` locally + refreshHeader. For reject, check if room already appears as `joined` in latest sync before calling leaveRoom.

### 5. Banned
- **Error:** `403 M_FORBIDDEN` on join
- **Symptom:** User was banned after being invited. Can still reject/leave (from `ban` membership → `leave` = "unbanned" transition).
- **Client action:** Surface errcode. Invite already invalid — offer "Hide from list".

### 6. Third-Party Invite — token invalid
- **Symptom:** key_validity_url check fails at the resident server. Join rejected.
- **Spec:** Only the resident server performs this check. "No other homeservers may reject the joining on the basis of key_validity_url."
- **Client action:** Surface error. "Hide from list".

### 7. Inviter Left the Room
- **Symptom:** Invite stays valid after inviter leaves. Server does NOT auto-revoke.
- **Spec:** "from invite → to leave" requires explicit `m.room.member` event with `membership: leave` on the invitee's `state_key`. Inviter leaving (own state_key) does not touch invitee's state.
- **Note:** Once inviter leaves, they cannot issue NEW invites, but existing invites remain.
- **Client action:** Accept still works. Reject still works (just leaveRoom). This is EXPECTED behavior — not a bug.

### 8. Tombstoned Room
- **Error:** None — invite stays valid, but accepting puts user in a dead/old room.
- **Spec:** Room upgrade creates `m.room.tombstone` in old room + `replacement_room` field. Power levels in old room modified to prevent new events. Membership events NOT transferred to new room.
- **Symptom:** Pending invites remain in old room. No automatic migration.
- **Client action:** If `m.room.tombstone` detected in invite_state, show warning: "This room was upgraded. Accepting will join the old room." Offer to join replacement room instead.

---

## API Reference

| API | Purpose | Precondition |
|---|---|---|
| `POST /rooms/{roomId}/join` | Accept invite / join room | User must be invited or room must be public |
| `POST /rooms/{roomId}/leave` | Reject invite / leave room | User must be invited or joined |
| `POST /rooms/{roomId}/forget` | Stop room appearing in /sync | User must have LEFT first (return 400 M_UNKNOWN if still joined) |

## Our Implementation Status

| Feature | Status | Commit |
|---|---|---|
| leaveRoom + error surfacing | Done | ad53681 |
| joinRoom idempotent handling | Done (stale state fix in room_handler.cpp acceptInvite) | 842b9cb |
| forgetRoom API | Done | b49d554 |
| Forget menu action | Done (but broken — doesn't chain leave→forget) | b49d554 |
| "Hide from list" for stuck rooms | **TODO** | — |
| Tombstone warning | Deferred | — |
| Restricted room condition check | Deferred | — |

## Key Lesson from Implementation

The `M_FORBIDDEN "duplicate auth_events for m.room.member"` error is a SERVER-SIDE state resolution bug — our client cannot fix it. The only client-side mitigation is: (1) surface the error clearly, (2) offer "Hide from list" to remove the room locally without touching the server, (3) log the full response for server admin debugging.

---

*Last updated: July 27, 2026. Research from Matrix v1.11 spec via webfetch + explore agent.*
