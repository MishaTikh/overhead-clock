#!/usr/bin/env bash
# Maintainer tool: make the repository public and switch on the protections GitHub only offers
# for public repositories on the free plan. Run once, after testing on hardware:
#
#   bash tools/make_public.sh
#
# Needs the GitHub CLI (gh), signed in as a repository admin.
set -euo pipefail
R="${1:-MishaTikh/overhead-clock}"

gh repo edit "$R" --visibility public --accept-visibility-change-consequences

# Outside contributors' pull requests wait for approval before any workflow runs.
gh api -X PUT "repos/$R/actions/permissions/fork-pr-contributor-approval" -f approval_policy=all_external_contributors

# Let people report vulnerabilities privately (Security tab > Report a vulnerability).
gh api -X PUT "repos/$R/private-vulnerability-reporting"

# Scan for leaked secrets and block pushes that contain them.
gh api -X PATCH "repos/$R" \
  -f 'security_and_analysis[secret_scanning][status]=enabled' \
  -f 'security_and_analysis[secret_scanning_push_protection][status]=enabled'

# main: CI must pass, history stays linear, no force-pushes or deletion.
gh api -X PUT "repos/$R/branches/main/protection" --input - <<'JSON'
{
  "required_status_checks": {"strict": true, "contexts": ["Logic tests (host)", "Firmware build (TC001)"]},
  "enforce_admins": false,
  "required_pull_request_reviews": null,
  "restrictions": null,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "required_linear_history": true
}
JSON

echo "Public, with fork-PR approval, private vulnerability reporting, secret scanning and branch protection on."
