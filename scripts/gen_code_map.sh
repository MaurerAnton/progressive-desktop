#!/usr/bin/env bash
# scripts/gen_code_map.sh — regenerate with: ./scripts/gen_code_map.sh > code_map.json
# Commit code_map.json so AI always has a current map.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

cat <<'EOF'
{
  "reactions": [
EOF
grep -rn "doQuickReact\|sendReaction\|addReaction\|removeReaction\|m.reaction\|ReactionData\|quickReact\|reactionClicked" \
    src/ui/ src/core/ --include="*.cpp" --include="*.hpp" 2>/dev/null | \
    grep -v '#include' | grep -v 'LOG(' | head -20 | \
    awk -F: '{gsub(/"/, "\\\"", $3); gsub(/^[ \t]+/, "", $3); printf "    \"%s:%s (%s)\",\n", $1, $2, substr($3,1,60)}' | \
    sed '$s/,$//'
cat <<'EOF'
  ],
  "room_switching": [
EOF
grep -rn "onRoomClicked\|setCurrentRoom\|currentRoomId_\|currentRoomIdStr_\|roomSwitchRequested\|clearCurrentRoom" \
    src/ui/ --include="*.cpp" --include="*.hpp" 2>/dev/null | \
    grep -v '#include' | grep -v 'LOG(' | head -15 | \
    awk -F: '{gsub(/"/, "\\\"", $3); gsub(/^[ \t]+/, "", $3); printf "    \"%s:%s (%s)\",\n", $1, $2, substr($3,1,60)}' | \
    sed '$s/,$//'
cat <<'EOF'
  ],
  "sync": [
EOF
grep -rn "FastSyncResponse\|syncLoop\|onSync\|SyncEngine::\|SyncEngineState\|wireSyncCallbacks\|batchLoadRoomStates\|prepareRoomSyncUpdate\|applyRoomSyncUpdate" \
    src/core/sync_engine.* src/core/fast_sync.* src/ui/ --include="*.cpp" --include="*.hpp" 2>/dev/null | \
    grep -v '#include' | grep -v 'LOG(' | head -20 | \
    awk -F: '{gsub(/"/, "\\\"", $3); gsub(/^[ \t]+/, "", $3); printf "    \"%s:%s (%s)\",\n", $1, $2, substr($3,1,60)}' | \
    sed '$s/,$//'
cat <<'EOF'
  ],
  "login_auth": [
EOF
grep -rn "showLoginDialog\|forceReLogin\|onLoginDialogAccepted\|LoginDialog\|loggedIn\|loggedOut\|AuthHandler" \
    src/ui/ --include="*.cpp" --include="*.hpp" 2>/dev/null | \
    grep -v '#include' | grep -v 'LOG(' | head -15 | \
    awk -F: '{gsub(/"/, "\\\"", $3); gsub(/^[ \t]+/, "", $3); printf "    \"%s:%s (%s)\",\n", $1, $2, substr($3,1,60)}' | \
    sed '$s/,$//'
cat <<'EOF'
  ],
  "e2ee": [
EOF
grep -rn "decryptor\|MegolmStore\|OlmAccount\|megolm\|isEncrypted\|isInitialized\|E2eeInit\|persistCrypto\|uploadDeviceKeys\|m\.room\.encrypted\|m\.room_key" \
    src/core/crypto/ src/core/sync_engine.* src/ui/ --include="*.cpp" --include="*.hpp" 2>/dev/null | \
    grep -v '#include' | grep -v 'LOG(' | head -20 | \
    awk -F: '{gsub(/"/, "\\\"", $3); gsub(/^[ \t]+/, "", $3); printf "    \"%s:%s (%s)\",\n", $1, $2, substr($3,1,60)}' | \
    sed '$s/,$//'
cat <<'EOF'
  ],
  "message_sending": [
EOF
grep -rn "doSend\|sendMessage\|doAttachFile\|slashCommand\|ChatView::" \
    src/ui/chat/ --include="*.cpp" --include="*.hpp" 2>/dev/null | \
    grep -v '#include' | grep -v 'LOG(' | head -15 | \
    awk -F: '{gsub(/"/, "\\\"", $3); gsub(/^[ \t]+/, "", $3); printf "    \"%s:%s (%s)\",\n", $1, $2, substr($3,1,60)}' | \
    sed '$s/,$//'
cat <<'EOF'
  ],
  "timeline_rendering": [
EOF
grep -rn "drawMessageBubble\|computeLayout\|BubbleLayout\|fastEventToDisplayed\|DisplayedEvent\|appendBack\|appendBackBatch\|TimelineDelegate\|TimelineModel" \
    src/ui/timeline/ --include="*.cpp" --include="*.hpp" 2>/dev/null | \
    grep -v '#include' | grep -v 'LOG(' | head -20 | \
    awk -F: '{gsub(/"/, "\\\"", $3); gsub(/^[ \t]+/, "", $3); printf "    \"%s:%s (%s)\",\n", $1, $2, substr($3,1,60)}' | \
    sed '$s/,$//'
cat <<'EOF'
  ],
  "room_list": [
EOF
grep -rn "RoomListModel\|RoomListDelegate\|RoomData\|InvitedRoom\|inviteAccepted\|inviteRejected\|updateHeader\|upsertRoom\|removeRoom" \
    src/ui/ --include="*.cpp" --include="*.hpp" 2>/dev/null | \
    grep -v '#include' | grep -v 'LOG(' | head -15 | \
    awk -F: '{gsub(/"/, "\\\"", $3); gsub(/^[ \t]+/, "", $3); printf "    \"%s:%s (%s)\",\n", $1, $2, substr($3,1,60)}' | \
    sed '$s/,$//'
cat <<'EOF'
  ],
  "handlers": [
EOF
grep -rn "class.*Handler\|class.*Switcher\|class.*Bootstrap" \
    src/ui/handlers/ --include="*.hpp" 2>/dev/null | \
    head -20 | \
    awk -F: '{gsub(/"/, "\\\"", $3); gsub(/^[ \t]+/, "", $3); printf "    \"%s:%s (%s)\",\n", $1, $2, substr($3,1,60)}' | \
    sed '$s/,$//'
echo '  ]'
echo '}'
