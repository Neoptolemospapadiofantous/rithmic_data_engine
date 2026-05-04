#!/usr/bin/env bash
# use-env.sh — Rithmic environment switcher (replaces scripts/use_env.py)
#
# Reads RITHMIC_ENV_{NAME}_ORDER_* and RITHMIC_ENV_{NAME}_MD_* from .env,
# writes active aliases used by the C++ executor:
#   ORDER_PLANT  → RITHMIC_LEGENDS_USER / PASSWORD / SYSTEM / URL / ACCOUNT
#   TICKER_PLANT → RITHMIC_AMP_USER / PASSWORD / SYSTEM / URL
#
# Usage:
#   ./scripts/use-env.sh              # show current active environment
#   ./scripts/use-env.sh legends      # switch to Legends live
#   ./scripts/use-env.sh tradeify     # switch to Tradeify
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ENV_FILE="$REPO/.env"

RED='\033[31m'; GREEN='\033[32m'; YELLOW='\033[33m'; CYAN='\033[36m'; BOLD='\033[1m'; RESET='\033[0m'

if [[ ! -f "$ENV_FILE" ]]; then
    echo -e "${RED}ERROR:${RESET} .env not found at $ENV_FILE" >&2; exit 1
fi

# ── Read .env into associative array ─────────────────────────────────────────
declare -A ENV_VALS
while IFS='=' read -r key val; do
    [[ -z "$key" || "$key" == \#* ]] && continue
    ENV_VALS["$key"]="$val"
done < <(grep -v '^#' "$ENV_FILE" | grep '=' || true)

# ── Status (no args) ─────────────────────────────────────────────────────────
if [[ $# -eq 0 ]]; then
    active="${ENV_VALS[RITHMIC_ACTIVE_ENV]:-"(not set)"}"
    echo -e "\n${BOLD}Active environment:${RESET}  ${CYAN}${active}${RESET}"
    echo -e "\n${BOLD}ORDER_PLANT (RITHMIC_LEGENDS_*)${RESET}"
    for field in USER SYSTEM URL ACCOUNT; do
        key="RITHMIC_LEGENDS_${field}"
        val="${ENV_VALS[$key]:-"(not set)"}"
        printf "  %-38s %s\n" "$key" "$val"
    done
    echo -e "\n${BOLD}TICKER_PLANT / AMP (RITHMIC_AMP_*)${RESET}"
    for field in USER SYSTEM URL; do
        key="RITHMIC_AMP_${field}"
        val="${ENV_VALS[$key]:-"(not set)"}"
        printf "  %-38s %s\n" "$key" "$val"
    done
    # List available envs
    available=$(grep "^RITHMIC_ENV_" "$ENV_FILE" | sed 's/^RITHMIC_ENV_\([^_]*\)_.*/\1/' | tr '[:upper:]' '[:lower:]' | sort -u | tr '\n' ' ')
    [[ -n "$available" ]] && echo -e "\n${BOLD}Available:${RESET}  $available"
    echo
    exit 0
fi

# ── Switch ────────────────────────────────────────────────────────────────────
NAME_LOWER="${1,,}"
NAME_UPPER="${1^^}"

# Collect ORDER and MD fields
declare -A ORDER_FIELDS MD_FIELDS
for key in "${!ENV_VALS[@]}"; do
    prefix="RITHMIC_ENV_${NAME_UPPER}_ORDER_"
    if [[ "$key" == "${prefix}"* ]]; then
        field="${key#"$prefix"}"
        ORDER_FIELDS["$field"]="${ENV_VALS[$key]}"
    fi
    prefix="RITHMIC_ENV_${NAME_UPPER}_MD_"
    if [[ "$key" == "${prefix}"* ]]; then
        field="${key#"$prefix"}"
        MD_FIELDS["$field"]="${ENV_VALS[$key]}"
    fi
done

if [[ ${#ORDER_FIELDS[@]} -eq 0 && ${#MD_FIELDS[@]} -eq 0 ]]; then
    echo -e "${RED}ERROR:${RESET} environment '${NAME_LOWER}' not found in .env" >&2
    available=$(grep "^RITHMIC_ENV_" "$ENV_FILE" | sed 's/^RITHMIC_ENV_\([^_]*\)_.*/\1/' | tr '[:upper:]' '[:lower:]' | sort -u | tr '\n' ' ')
    [[ -n "$available" ]] && echo "  Available: $available" >&2
    exit 1
fi

# Build update map: alias → value
declare -A UPDATES
UPDATES["RITHMIC_ACTIVE_ENV"]="$NAME_UPPER"
UPDATES["RITHMIC_LEGENDS_USER"]="${ORDER_FIELDS[USER]:-}"
UPDATES["RITHMIC_LEGENDS_PASSWORD"]="${ORDER_FIELDS[PASSWORD]:-}"
UPDATES["RITHMIC_LEGENDS_SYSTEM"]="${ORDER_FIELDS[SYSTEM]:-}"
UPDATES["RITHMIC_LEGENDS_URL"]="${ORDER_FIELDS[URL]:-}"
UPDATES["RITHMIC_LEGENDS_ACCOUNT"]="${ORDER_FIELDS[ACCOUNT]:-}"
UPDATES["RITHMIC_AMP_USER"]="${MD_FIELDS[USER]:-}"
UPDATES["RITHMIC_AMP_PASSWORD"]="${MD_FIELDS[PASSWORD]:-}"
UPDATES["RITHMIC_AMP_SYSTEM"]="${MD_FIELDS[SYSTEM]:-}"
UPDATES["RITHMIC_AMP_URL"]="${MD_FIELDS[URL]:-}"

# Write updates back to .env (in-place, preserving comments)
tmpfile=$(mktemp)
updated_keys=""
while IFS= read -r line; do
    if [[ "$line" =~ ^([A-Za-z_][A-Za-z0-9_]*)= ]]; then
        k="${BASH_REMATCH[1]}"
        if [[ -v UPDATES["$k"] ]]; then
            echo "${k}=${UPDATES[$k]}" >> "$tmpfile"
            updated_keys="$updated_keys $k"
            continue
        fi
    fi
    echo "$line" >> "$tmpfile"
done < "$ENV_FILE"

# Append any keys not already present in the file
for k in "${!UPDATES[@]}"; do
    if [[ "$updated_keys" != *" $k"* ]]; then
        echo "${k}=${UPDATES[$k]}" >> "$tmpfile"
    fi
done

mv "$tmpfile" "$ENV_FILE"

# Apply per-env JSON config overrides using jq
override="$REPO/config/envs/${NAME_LOWER}.json"
if [[ -f "$override" ]]; then
    for cfg_name in live_config.json MNQ_config.json MES_config.json MYM_config.json; do
        cfg_path="$REPO/config/$cfg_name"
        [[ -f "$cfg_path" ]] || continue
        # Deep merge: override keys into existing config, skip _comment keys
        merged=$(jq -s '
            def merge: .[0] as $base | .[1] as $over |
              ($over | to_entries | map(select(.key | startswith("_") | not))) as $pairs |
              $pairs | reduce .[] as $e ($base; .[$e.key] = $e.value);
            merge
        ' "$cfg_path" "$override")
        if [[ "$merged" != "$(cat "$cfg_path")" ]]; then
            echo "$merged" > "$cfg_path"
            echo "  Updated $cfg_name"
        fi
    done
    echo -e "  Applied config overrides from config/envs/${NAME_LOWER}.json"
else
    echo -e "  ${YELLOW}No config override at config/envs/${NAME_LOWER}.json${RESET}"
fi

echo -e "\n${GREEN}✓${RESET}  Switched to ${BOLD}${NAME_UPPER}${RESET}"
echo -e "  ORDER_PLANT  → user=${ORDER_FIELDS[USER]:-?}  system=${ORDER_FIELDS[SYSTEM]:-?}"
echo -e "  TICKER_PLANT → user=${MD_FIELDS[USER]:-?}  system=${MD_FIELDS[SYSTEM]:-?}"
echo
